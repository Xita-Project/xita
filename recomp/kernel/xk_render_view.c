/* xk_render_view.c - see xk_render_view.h.
 *
 * Learning: armed once gameplay is detected (the gap between a scene exit and the next scene entry,
 * i.e. tick + present, exceeds XV_RENDER_VIEW_LEARN_GAP_US for 30 frames; menus are ~1 ms) or with
 * XV_RENDER_VIEW_LEARN_NOW=1. A pass hashes every physical + image page at one Present and again at
 * the next: changed physical pages get a shadow slot, changed image pages join the image list.
 * XV_RENDER_VIEW_LEARN_PASSES passes, XV_RENDER_VIEW_LEARN_INTERVAL frames apart, ~0.7 s stall each.
 * Boundary (scene entry, outermost only): copy every listed page live -> shadow slot / image copy,
 * refresh XV_RENDER_VIEW_ROLL unlisted image pages of the .data tail per frame (rotating, so a rarely
 * written global is stale for at most span/ROLL frames), keep a pristine copy of the pages the scene
 * is known to write, bind the thread to the render table. Exit: bind back, merge every word the scene
 * changed (copy != pristine) into live. Full frames (first 30 active, then every
 * XV_RENDER_VIEW_FULL_INTERVAL) take pristine copies of and compare every listed page, growing the
 * scene-write set; other frames only handle the set. XV_RENDER_VIEW_COPYBACK=0 skips the merge. */
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
static int configured, ready, copyback = 1;
static xk_render_view_layout L;
static struct { uint8_t *img_base; uint32_t entries[1u << 20]; } render_block;
static uint32_t *render_pt = render_block.entries;

/* listed physical pages -> shadow slot (0xFFFF none); listed image pages; scene-write sets */
static uint16_t *slot_of;                 /* [phys_pages] */
static uint32_t *slot_page;               /* [shadow_pages] arena page index per slot */
static unsigned slots_used, slots_overflow;
static uint8_t *img_listed;               /* [image_pages] */
static uint32_t *img_list; static unsigned img_listed_n;
static uint32_t img_lo = UINT32_MAX;      /* lowest written image page: start of the .data tail */
static uint32_t roll_next; static unsigned roll_pages = 32;
static uint8_t *sw_slot, *sw_img;         /* [shadow_pages], [image_pages]: scene writes these */
static unsigned sw_slots, sw_imgs, sw_found_late;
static uint8_t *pristine;                 /* [shadow_pages + image_pages] pages, host memory */
static unsigned full_interval = 30, active_frames;

/* learning */
static unsigned learn_interval = 150, learn_passes = 6, learn_done, learn_state, learn_next_frame, learn_armed, gap_frames;
static uint32_t learn_gap_us = 8000;
static uint32_t *learn_hash;
static uint64_t learn_us, last_leave_us;

/* per-report counters */
static unsigned depth, frames_entered, full_frames, mirrors, mirrors_dropped, aliases_max;
static uint64_t enter_us, leave_us, bytes_in, words_merged;

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
static void retarget_page(uint32_t off)
{
    uint32_t vps[8]; unsigned n = xk_mem_page_aliases(off, vps, 8);
    if (n > aliases_max) aliases_max = n;
    for (unsigned i = 0; i < n; ++i) render_pt[vps[i]] = render_off_for(off);
}
static void learn_page(uint32_t page)
{
    uint32_t off = page * XK_PAGE;
    if (off >= L.image_off) {
        uint32_t ip = page - (L.image_off >> 12);
        if (ip < img_lo) { img_lo = ip; roll_next = ip; }
        if (!img_listed[ip]) { img_listed[ip] = 1; img_list[img_listed_n++] = ip; }
        return;
    }
    if (slot_of[page] != 0xFFFFu) return;
    if (slots_used >= L.shadow_pages) { slots_overflow++; return; }
    slot_of[page] = (uint16_t)slots_used; slot_page[slots_used++] = page;
    retarget_page(off);
}
static void bench(void)
{
    unsigned N = 1u << 20;                              /* bounded by the arena regions it borrows */
    if (L.shadow_pages * XK_PAGE < N) N = L.shadow_pages * XK_PAGE;
    if (L.image_pages * XK_PAGE < N) N = L.image_pages * XK_PAGE;
    if (N < 64u * 1024u) return;
    uint8_t *heap = malloc(2 * N); if (!heap) return;
    uint8_t *a = g_xram + L.shadow_off, *b = g_xram + L.image_copy_off;   /* arena regions, cold */
    uint64_t t; double mbps[4];
    t = xk_os_monotonic_us(); memcpy(a, b, N); mbps[0] = N / 1.048576 / (double)(xk_os_monotonic_us() - t + 1);
    t = xk_os_monotonic_us(); memcpy(heap, b, N); mbps[1] = N / 1.048576 / (double)(xk_os_monotonic_us() - t + 1);
    t = xk_os_monotonic_us(); memcpy(heap + N, heap, N); mbps[2] = N / 1.048576 / (double)(xk_os_monotonic_us() - t + 1);
    t = xk_os_monotonic_us(); (void)memcmp(heap, heap + N, N); mbps[3] = N / 1.048576 / (double)(xk_os_monotonic_us() - t + 1);
    XK_LOG("[render-view] %u KiB throughput MB/s: arena->arena %.0f, arena->heap %.0f, heap->heap %.0f, memcmp heap %.0f\n", N >> 10, mbps[0], mbps[1], mbps[2], mbps[3]);
    free(heap);
}
void xv_render_view_configure(void)
{
    if (configured) return;
    configured = 1;
    const char *e = getenv("XV_RENDER_VIEW"); xv_render_view_enabled = e ? atoi(e) != 0 : XV_RENDER_VIEW_DEFAULT;
    if ((e = getenv("XV_RENDER_VIEW_COPYBACK"))) copyback = atoi(e) != 0;
    if ((e = getenv("XV_RENDER_VIEW_LEARN_INTERVAL"))) learn_interval = (unsigned)atoi(e);
    if ((e = getenv("XV_RENDER_VIEW_LEARN_PASSES"))) learn_passes = (unsigned)atoi(e);
    if ((e = getenv("XV_RENDER_VIEW_LEARN_GAP_US"))) learn_gap_us = (uint32_t)atoi(e);
    if ((e = getenv("XV_RENDER_VIEW_LEARN_NOW"))) learn_armed = atoi(e) != 0;
    if ((e = getenv("XV_RENDER_VIEW_ROLL"))) roll_pages = (unsigned)atoi(e);
    if ((e = getenv("XV_RENDER_VIEW_FULL_INTERVAL"))) full_interval = (unsigned)atoi(e);
    xk_mem_render_view_layout(&L);
    if (!xv_render_view_enabled) { XK_LOG("[render-view] process-start disabled\n"); return; }
    if (!L.shadow_pages || !g_xram) { XK_LOG("[render-view] no shadow region; disabled\n"); xv_render_view_enabled = 0; return; }
    slot_of = malloc(L.phys_pages * sizeof *slot_of); slot_page = malloc(L.shadow_pages * sizeof *slot_page);
    img_listed = calloc(L.image_pages, 1); img_list = malloc(L.image_pages * sizeof *img_list);
    sw_slot = calloc(L.shadow_pages, 1); sw_img = calloc(L.image_pages, 1);
    learn_hash = malloc((L.phys_pages + L.image_pages) * sizeof *learn_hash);
    pristine = malloc((size_t)(L.shadow_pages + L.image_pages) * XK_PAGE);
    if (!slot_of || !slot_page || !img_listed || !img_list || !sw_slot || !sw_img || !learn_hash || !pristine) { XK_LOG("[render-view] no memory; disabled\n"); xv_render_view_enabled = 0; return; }
    memset(slot_of, 0xFF, L.phys_pages * sizeof *slot_of);
    memcpy(render_pt, g_xpt, (1u << 20) * sizeof *render_pt);
    for (uint32_t ip = 0; ip < L.image_pages; ++ip) {
        uint32_t vp = L.image_vpage + ip, off = L.image_off + ip * XK_PAGE;
        if (g_xpt[vp] == off) render_pt[vp] = L.image_copy_off + ip * XK_PAGE;
    }
    memcpy(g_xram + L.image_copy_off, g_xram + L.image_off, L.image_pages * XK_PAGE);
    render_block.img_base = g_xram + L.image_copy_off - (L.image_vpage << 12);
    ready = 1;
    bench();
    XK_LOG("[render-view] process-start enabled; shadow %u pages at %08X, image copy %u pages at %08X; learn %u passes every %u frames after a %u us tick gap%s; roll %u, full every %u, copyback %d\n",
           L.shadow_pages, L.shadow_off, L.image_pages, L.image_copy_off, learn_passes, learn_interval, learn_gap_us, learn_armed ? " (armed now)" : "", roll_pages, full_interval, copyback);
}
static void bind_table(uint32_t *table)
{
#if defined(__vita__) && defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    extern void xv_thread_bind_table(uint32_t *); xv_thread_bind_table(table);
#elif defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    xv_host_page_table = table;  /* host fixture: the current thread's table */
#else
    (void)table;
#endif
}
void xv_render_view_present(unsigned frame)
{
    if (!ready || !learn_armed || learn_done >= learn_passes) return;
    unsigned pages = L.phys_pages + L.image_pages;
    if (learn_state == 0) {
        if (frame < learn_next_frame) return;
        uint64_t t0 = xk_os_monotonic_us();
        for (unsigned i = 0; i < pages; ++i) learn_hash[i] = page_hash(g_xram + (size_t)i * XK_PAGE);
        learn_us += xk_os_monotonic_us() - t0; learn_state = 1; return;
    }
    uint64_t t0 = xk_os_monotonic_us(); unsigned changed = 0, before = slots_used, ibefore = img_listed_n;
    for (unsigned i = 0; i < pages; ++i) if (page_hash(g_xram + (size_t)i * XK_PAGE) != learn_hash[i]) { changed++; learn_page(i); }
    learn_us += xk_os_monotonic_us() - t0; learn_state = 0; learn_done++; learn_next_frame = frame + learn_interval;
    XK_LOG("[render-view] learn pass %u/%u at frame %u: %u pages changed, +%u shadow slots (%u used, %u overflow), +%u image pages (%u listed, tail from page %u of %u), %llu us so far\n",
           learn_done, learn_passes, frame, changed, slots_used - before, slots_used, slots_overflow, img_listed_n - ibefore, img_listed_n,
           img_lo == UINT32_MAX ? 0 : img_lo, L.image_pages, (unsigned long long)learn_us);
}
static inline uint8_t *shadow_of(unsigned s) { return g_xram + L.shadow_off + (size_t)s * XK_PAGE; }
static inline uint8_t *live_of_slot(unsigned s) { return g_xram + (size_t)slot_page[s] * XK_PAGE; }
static inline uint8_t *copy_of_img(uint32_t ip) { return g_xram + L.image_copy_off + (size_t)ip * XK_PAGE; }
static inline uint8_t *live_of_img(uint32_t ip) { return g_xram + L.image_off + (size_t)ip * XK_PAGE; }
static inline uint8_t *pristine_slot(unsigned s) { return pristine + (size_t)s * XK_PAGE; }
static inline uint8_t *pristine_img(uint32_t ip) { return pristine + ((size_t)L.shadow_pages + ip) * XK_PAGE; }
static int full_frame;

void xv_render_view_enter(unsigned *scope, void *context)
{
    (void)context;
    uint64_t t0 = xk_os_monotonic_us();
    if (!learn_armed && last_leave_us) {              /* gameplay heuristic: tick + present gap */
        if (t0 - last_leave_us > learn_gap_us) { if (++gap_frames >= 30) { learn_armed = 1; XK_LOG("[render-view] gameplay detected (tick gap > %u us for 30 frames): learning armed\n", learn_gap_us); } }
        else gap_frames = 0;
    }
    if (!ready || depth++) return;
    if (!learn_done) return;
    *scope = 1;
    full_frame = active_frames < 30 || (full_interval && active_frames % full_interval == 0);
    active_frames++;
    for (unsigned s = 0; s < slots_used; ++s) {
        memcpy(shadow_of(s), live_of_slot(s), XK_PAGE);
        if (full_frame || sw_slot[s]) memcpy(pristine_slot(s), shadow_of(s), XK_PAGE);
    }
    for (unsigned i = 0; i < img_listed_n; ++i) {
        uint32_t ip = img_list[i];
        memcpy(copy_of_img(ip), live_of_img(ip), XK_PAGE);
        if (full_frame || sw_img[ip]) memcpy(pristine_img(ip), copy_of_img(ip), XK_PAGE);
    }
    unsigned rolled = 0;
    if (img_lo < L.image_pages)
        for (unsigned n = 0; n < roll_pages && rolled < L.image_pages - img_lo; ++n) {   /* unlisted tail pages, rotating */
            if (roll_next >= L.image_pages) roll_next = img_lo;
            uint32_t ip = roll_next++;
            if (!img_listed[ip]) { memcpy(copy_of_img(ip), live_of_img(ip), XK_PAGE); rolled++; }
        }
    bytes_in += ((uint64_t)slots_used + img_listed_n + rolled) * XK_PAGE;
    bind_table(render_pt);
    enter_us += xk_os_monotonic_us() - t0; frames_entered++; full_frames += full_frame;
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
    uint64_t t0 = xk_os_monotonic_us();
    if (!ready || !depth) { last_leave_us = t0; return; }
    if (--depth || !*scope) { last_leave_us = t0; return; }
    bind_table(g_xpt);
    for (unsigned s = 0; s < slots_used; ++s) {
        if (!full_frame && !sw_slot[s]) continue;
        if (!merge_page(shadow_of(s), pristine_slot(s), live_of_slot(s))) continue;
        if (!sw_slot[s]) { sw_slot[s] = 1; sw_slots++; if (active_frames > 30) sw_found_late++; }
    }
    for (unsigned i = 0; i < img_listed_n; ++i) {
        uint32_t ip = img_list[i];
        if (!full_frame && !sw_img[ip]) continue;
        if (!merge_page(copy_of_img(ip), pristine_img(ip), live_of_img(ip))) continue;
        if (!sw_img[ip]) { sw_img[ip] = 1; sw_imgs++; if (active_frames > 30) sw_found_late++; }
    }
    last_leave_us = xk_os_monotonic_us();
    leave_us += last_leave_us - t0;
}
void xv_render_view_report(unsigned frames)
{
    if (!ready) return;
    XK_LOG("[render-view] %u frames: entered %u (full %u); slots %u (overflow %u) image pages %u listed, tail %u; copy-in %.2f ms/frame (%.0f KiB), merge %.2f ms/frame (%llu words); scene-write set %u slots + %u image pages (%u found late); mirrors %u dropped %u; aliases max %u\n",
           frames, frames_entered, full_frames, slots_used, slots_overflow, img_listed_n, img_lo < L.image_pages ? L.image_pages - img_lo : 0,
           frames_entered ? (double)enter_us / frames_entered / 1000.0 : 0.0, frames_entered ? (double)bytes_in / frames_entered / 1024.0 : 0.0,
           frames_entered ? (double)leave_us / frames_entered / 1000.0 : 0.0, (unsigned long long)words_merged,
           sw_slots, sw_imgs, sw_found_late, mirrors, mirrors_dropped, aliases_max);
    frames_entered = full_frames = 0; enter_us = leave_us = bytes_in = words_merged = 0; mirrors = mirrors_dropped = 0;
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
