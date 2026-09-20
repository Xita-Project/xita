/* xk_render_view.c - see xk_render_view.h.
 *
 * Learning: a census pass hashes every physical + image arena page at one Present and again at the
 * next; pages whose hash changed join the dirty set (physical pages get a shadow slot each; image pages
 * widen the copied image span). XV_RENDER_VIEW_LEARN_PASSES passes, XV_RENDER_VIEW_LEARN_INTERVAL
 * frames apart, each stalling two frames by ~0.7 s: experiment-grade, not for shipping.
 * Boundary (scene entry, outermost only): copy each dirty physical page live -> shadow slot, copy the
 * dirty image span live -> render image copy (and both -> a pristine host copy), bind the thread to
 * the render table. Exit: bind back, then merge: every word the scene changed (shadow != pristine) is
 * written to live; words it left alone keep whatever live holds now, so hand-written helpers that
 * wrote live directly are not clobbered and (increment C) the next tick's writes survive.
 * XV_RENDER_VIEW_COPYBACK=0 skips the merge (diagnostic). */
#include "xk.h"
#include "xk_render_view.h"
#include <stdlib.h>
#include <string.h>
#include "../xv_x86rt.h"
#if XV_RENDER_VIEW

#ifndef XV_RENDER_VIEW_DEFAULT
#define XV_RENDER_VIEW_DEFAULT 0
#endif
int xv_render_view_enabled;
static int configured, ready, copyback = 1, verify_sample;
static xk_render_view_layout L;
static struct { uint8_t *img_base; uint32_t entries[1u << 20]; } render_block;
static uint32_t *render_pt = render_block.entries;

/* dirty physical pages -> shadow slot index (0xFFFF = none); dirty image span in pages */
static uint16_t *slot_of;                 /* [phys_pages] */
static uint32_t *slot_page;               /* [shadow_pages] arena page index of each used slot */
static unsigned slots_used, slots_overflow;
static uint32_t img_dirty_lo = UINT32_MAX, img_dirty_hi;   /* image page indices [lo, hi) */
static uint8_t *scene_wrote;              /* [phys_pages] union of pages the scene wrote (diagnostic) */
static uint8_t *pristine;                 /* host copy of every shadow page + the image span as copied in */
static uint64_t words_merged;
static unsigned scene_wrote_pages, scene_wrote_img_pages;

/* learning */
static unsigned learn_interval = 150, learn_passes = 6, learn_done, learn_state, learn_next_frame;
static uint32_t *learn_hash;              /* [phys_pages + image_pages] */
static uint64_t learn_us;

/* per-report counters */
static unsigned depth, frames_entered, mirrors, mirrors_dropped, aliases_max;
static uint64_t enter_us, leave_us, bytes_in;

static uint32_t page_hash(const uint8_t *p)
{
    const uint32_t *w = (const uint32_t *)p; uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 1024; i += 4) { h = (h ^ w[i]) * 16777619u; h = (h ^ w[i+1]) * 16777619u; h = (h ^ w[i+2]) * 16777619u; h = (h ^ w[i+3]) * 16777619u; }
    return h;
}
static uint32_t render_off_for(uint32_t off)      /* live arena offset -> render-table offset */
{
    if (off >= L.image_off && off < L.trash_off) return L.image_copy_off + (off - L.image_off);
    if (off < L.image_off && slot_of && slot_of[off >> 12] != 0xFFFFu) return L.shadow_off + (uint32_t)slot_of[off >> 12] * XK_PAGE;
    return off;
}
void xv_render_view_mirror(uint32_t vpage, uint32_t arena_off)
{
    if (!ready) return;
    if (depth) { mirrors_dropped++; return; }         /* never retarget the table the scene is reading */
    render_pt[vpage] = render_off_for(arena_off); mirrors++;
}
static void retarget_page(uint32_t off)             /* (re)point every alias of a physical page */
{
    uint32_t vps[8]; unsigned n = xk_mem_page_aliases(off, vps, 8);
    if (n > aliases_max) aliases_max = n;
    for (unsigned i = 0; i < n; ++i) render_pt[vps[i]] = render_off_for(off);
}
static void learn_page(uint32_t page)               /* arena page index that changed between Presents */
{
    uint32_t off = page * XK_PAGE;
    if (off >= L.image_off) {
        uint32_t ip = page - (L.image_off >> 12);
        if (ip < img_dirty_lo) img_dirty_lo = ip;       /* the span runs from the lowest written page */
        img_dirty_hi = L.image_pages;                     /* to the end of the image (.data/.bss tail), so */
        return;                                           /* rarely written globals are never stale */
    }
    if (slot_of[page] != 0xFFFFu) return;
    if (slots_used >= L.shadow_pages) { slots_overflow++; return; }
    slot_of[page] = (uint16_t)slots_used; slot_page[slots_used++] = page;
    retarget_page(off);
}
void xv_render_view_configure(void)
{
    if (configured) return;
    configured = 1;
    const char *e = getenv("XV_RENDER_VIEW"); xv_render_view_enabled = e ? atoi(e) != 0 : XV_RENDER_VIEW_DEFAULT;
    if ((e = getenv("XV_RENDER_VIEW_COPYBACK"))) copyback = atoi(e) != 0;
    if ((e = getenv("XV_RENDER_VIEW_LEARN_INTERVAL"))) learn_interval = (unsigned)atoi(e);
    if ((e = getenv("XV_RENDER_VIEW_LEARN_PASSES"))) learn_passes = (unsigned)atoi(e);
    if ((e = getenv("XV_RENDER_VIEW_VERIFY"))) verify_sample = atoi(e);
    xk_mem_render_view_layout(&L);
    if (!xv_render_view_enabled) { XK_LOG("[render-view] process-start disabled\n"); return; }
    if (!L.shadow_pages || !g_xram) { XK_LOG("[render-view] no shadow region; disabled\n"); xv_render_view_enabled = 0; return; }
    slot_of = malloc(L.phys_pages * sizeof *slot_of); slot_page = malloc(L.shadow_pages * sizeof *slot_page);
    scene_wrote = calloc(L.phys_pages, 1); learn_hash = malloc((L.phys_pages + L.image_pages) * sizeof *learn_hash);
    pristine = malloc((size_t)(L.shadow_pages + L.image_pages) * XK_PAGE);
    if (!slot_of || !slot_page || !scene_wrote || !learn_hash || !pristine) { XK_LOG("[render-view] no memory; disabled\n"); xv_render_view_enabled = 0; return; }
    memset(slot_of, 0xFF, L.phys_pages * sizeof *slot_of);
    /* render table = live table with image pages pointing at the image copy; image copy = image */
    memcpy(render_pt, g_xpt, (1u << 20) * sizeof *render_pt);
    for (uint32_t ip = 0; ip < L.image_pages; ++ip) {
        uint32_t vp = L.image_vpage + ip, off = L.image_off + ip * XK_PAGE;
        if (g_xpt[vp] == off) render_pt[vp] = L.image_copy_off + ip * XK_PAGE;
    }
    memcpy(g_xram + L.image_copy_off, g_xram + L.image_off, L.image_pages * XK_PAGE);
    render_block.img_base = g_xram + L.image_copy_off - (L.image_vpage << 12);
    ready = 1;
    XK_LOG("[render-view] process-start enabled; shadow %u pages at %08X, image copy %u pages at %08X, learn %u passes every %u frames, copyback %d\n",
           L.shadow_pages, L.shadow_off, L.image_pages, L.image_copy_off, learn_passes, learn_interval, copyback);
}
static void bind_table(uint32_t *table)
{
#if defined(__vita__) && defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    extern void xv_thread_bind_table(uint32_t *); xv_thread_bind_table(table);
#elif defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    xv_host_page_table = table;  /* host fixture: the current thread's table */
#else
    (void)table;                 /* single table: nothing to bind */
#endif
}
void xv_render_view_present(unsigned frame)
{
    if (!ready || learn_done >= learn_passes) return;
    unsigned pages = L.phys_pages + L.image_pages;
    if (learn_state == 0) {
        if (frame < learn_next_frame) return;
        uint64_t t0 = xk_os_monotonic_us();
        for (unsigned i = 0; i < pages; ++i) learn_hash[i] = page_hash(g_xram + (size_t)i * XK_PAGE);
        learn_us += xk_os_monotonic_us() - t0; learn_state = 1; return;
    }
    uint64_t t0 = xk_os_monotonic_us(); unsigned changed = 0, before = slots_used;
    for (unsigned i = 0; i < pages; ++i) if (page_hash(g_xram + (size_t)i * XK_PAGE) != learn_hash[i]) { changed++; learn_page(i); }
    learn_us += xk_os_monotonic_us() - t0; learn_state = 0; learn_done++; learn_next_frame = frame + learn_interval;
    XK_LOG("[render-view] learn pass %u/%u at frame %u: %u pages changed, %u new shadow slots (%u used, %u overflow), image span pages %u..%u, %llu us so far\n",
           learn_done, learn_passes, frame, changed, slots_used - before, slots_used, slots_overflow,
           img_dirty_lo == UINT32_MAX ? 0 : img_dirty_lo, img_dirty_hi, (unsigned long long)learn_us);
}
void xv_render_view_enter(unsigned *scope, void *context)
{
    (void)context;
    if (!ready || depth++) return;
    if (!learn_done) return;                              /* nothing learned yet: keep the live table */
    *scope = 1;
    uint64_t t0 = xk_os_monotonic_us();
    for (unsigned s = 0; s < slots_used; ++s) {
        uint8_t *shadow = g_xram + L.shadow_off + (size_t)s * XK_PAGE;
        memcpy(shadow, g_xram + (size_t)slot_page[s] * XK_PAGE, XK_PAGE);
        memcpy(pristine + (size_t)s * XK_PAGE, shadow, XK_PAGE);
    }
    if (img_dirty_lo < img_dirty_hi) {
        size_t lo = (size_t)img_dirty_lo * XK_PAGE, n = (size_t)(img_dirty_hi - img_dirty_lo) * XK_PAGE;
        memcpy(g_xram + L.image_copy_off + lo, g_xram + L.image_off + lo, n);
        memcpy(pristine + (size_t)L.shadow_pages * XK_PAGE + lo, g_xram + L.image_copy_off + lo, n); bytes_in += n;
    }
    bytes_in += (uint64_t)slots_used * XK_PAGE;
    bind_table(render_pt);
    enter_us += xk_os_monotonic_us() - t0; frames_entered++;
}
/* Words the scene changed in its copy go to live; returns whether the page had any. */
static int merge_page(const uint8_t *copy, const uint8_t *pre, uint8_t *live)
{
    if (memcmp(copy, pre, XK_PAGE) == 0) return 0;
    const uint32_t *c = (const uint32_t *)copy, *p = (const uint32_t *)pre; uint32_t *l = (uint32_t *)live;
    if (copyback) for (unsigned i = 0; i < XK_PAGE / 4; ++i) if (c[i] != p[i]) { l[i] = c[i]; words_merged++; }
    return 1;
}
void xv_render_view_leave(unsigned *scope)
{
    if (!ready || !depth) return;
    if (--depth || !*scope) return;
    uint64_t t0 = xk_os_monotonic_us();
    bind_table(g_xpt);
    for (unsigned s = 0; s < slots_used; ++s) {
        uint8_t *shadow = g_xram + L.shadow_off + (size_t)s * XK_PAGE, *live = g_xram + (size_t)slot_page[s] * XK_PAGE;
        if (!merge_page(shadow, pristine + (size_t)s * XK_PAGE, live)) continue;
        if (!scene_wrote[slot_page[s]]) { scene_wrote[slot_page[s]] = 1; scene_wrote_pages++; }
    }
    if (img_dirty_lo < img_dirty_hi)
        for (uint32_t ip = img_dirty_lo; ip < img_dirty_hi; ++ip)
            scene_wrote_img_pages += merge_page(g_xram + L.image_copy_off + (size_t)ip * XK_PAGE,
                                                pristine + ((size_t)L.shadow_pages + ip) * XK_PAGE, g_xram + L.image_off + (size_t)ip * XK_PAGE);
    leave_us += xk_os_monotonic_us() - t0;
}
void xv_render_view_report(unsigned frames)
{
    if (!ready) return;
    XK_LOG("[render-view] %u frames: entered %u; slots %u (overflow %u) image span %u pages; copy-in %.2f ms/frame (%.0f KiB), merge %.2f ms/frame (%llu words); scene wrote %u phys pages (union) %u image-page events; mirrors %u dropped %u; aliases max %u\n",
           frames, frames_entered, slots_used, slots_overflow, img_dirty_lo < img_dirty_hi ? img_dirty_hi - img_dirty_lo : 0,
           frames_entered ? (double)enter_us / frames_entered / 1000.0 : 0.0, frames_entered ? (double)bytes_in / frames_entered / 1024.0 : 0.0,
           frames_entered ? (double)leave_us / frames_entered / 1000.0 : 0.0, (unsigned long long)words_merged,
           scene_wrote_pages, scene_wrote_img_pages, mirrors, mirrors_dropped, aliases_max);
    frames_entered = 0; enter_us = leave_us = bytes_in = 0; scene_wrote_img_pages = 0; mirrors = mirrors_dropped = 0; words_merged = 0;
}
#else
int xv_render_view_enabled;
void xv_render_view_configure(void) {}
void xv_render_view_present(unsigned frame) { (void)frame; }
void xv_render_view_enter(unsigned *scope, void *context) { (void)scope; (void)context; }
void xv_render_view_leave(unsigned *scope) { (void)scope; }
void xv_render_view_report(unsigned frames) { (void)frames; }
void xv_render_view_mirror(uint32_t vpage, uint32_t arena_off) { (void)vpage; (void)arena_off; }
#endif
