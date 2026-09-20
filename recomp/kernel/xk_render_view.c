/* xk_render_view.c - see xk_render_view.h.
 *
 * In-place mode (increment A, single-threaded): for the duration of the scene half the LIVE page
 * table's entries for the learned dirty pages point at per-frame shadow copies and the flat image base
 * points at a persistent image copy, so every thread (owner, native helper threads, other guest
 * fibers) reads and writes the same frozen view; on exit the entries are restored and every word the
 * scene changed is merged into the live pages. A per-thread table (TPIDRURW) is NOT used here: the
 * first cut bound only the owner and the scene spun forever on results helper threads had written
 * through their own table (handoff §28). The overlap (increment C) needs per-job binding instead.
 *
 * Learning: armed once gameplay is detected (scene-exit to scene-entry gap > XV_RENDER_VIEW_LEARN_GAP_US
 * for 30 frames) or XV_RENDER_VIEW_LEARN_NOW=1; a pass hashes every physical + image page at one
 * Present and the next; changed physical pages get a shadow slot, changed image pages join the list.
 * Boundary: copy listed pages live -> shadow / image copy, refresh XV_RENDER_VIEW_ROLL unlisted
 * .data-tail pages per frame (rotating), keep pristine copies of the pages the scene is known to
 * write (all listed pages on full frames: first 30 active, then every XV_RENDER_VIEW_FULL_INTERVAL),
 * retarget. Exit: restore, merge (copy != pristine -> live). XV_RENDER_VIEW_COPYBACK=0 skips the merge. */
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

static uint16_t *slot_of;                 /* [phys_pages] -> shadow slot or 0xFFFF */
static uint32_t *slot_page;               /* [shadow_pages] arena page index per slot */
static unsigned slots_used, slots_overflow;
static uint8_t *img_listed; static uint32_t *img_list; static unsigned img_listed_n;
static uint32_t img_lo = UINT32_MAX, roll_next; static unsigned roll_pages = 32;
static uint8_t *sw_slot, *sw_img; static unsigned sw_slots, sw_imgs, sw_found_late;
static uint8_t *pristine;                 /* [shadow_pages + image_pages] pages, host memory */
static unsigned full_interval = 30, active_frames;
static uint8_t *live_img_base;
static uint32_t phys_limit = 4u << 20;      /* shadow only physical pages below this (game state); D3D/audio buffers and guest stacks stay live */
static int image_view = 1;                  /* shadow the image .data tail too */
static volatile uint64_t bound_since_us; static unsigned watchdog_trips;

/* entries retargeted for the current scene: (vpage, live offset, view offset) */
typedef struct { uint32_t vp, live, view; } retarget_t;
static retarget_t *retargets; static unsigned retargets_n, retargets_last, retargets_max, retargets_overflow, image_entries;

static unsigned learn_interval = 150, learn_passes = 6, learn_done, learn_state, learn_next_frame, learn_armed, gap_frames;
static uint32_t learn_gap_us = 8000; static uint32_t *learn_hash; static uint64_t learn_us, last_leave_us;

static unsigned depth, bound, frames_entered, full_frames, fiber_switches, aliases_max, mirrors_in_scene;
static uint64_t enter_us, leave_us, bytes_in, words_merged;
static int full_frame;

static uint32_t page_hash(const uint8_t *p)
{
    const uint32_t *w = (const uint32_t *)p; uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 1024; i += 4) { h = (h ^ w[i]) * 16777619u; h = (h ^ w[i+1]) * 16777619u; h = (h ^ w[i+2]) * 16777619u; h = (h ^ w[i+3]) * 16777619u; }
    return h;
}
void xv_render_view_mirror(uint32_t vpage, uint32_t arena_off) { (void)vpage; (void)arena_off; if (bound) mirrors_in_scene++; }
static void learn_page(uint32_t page)
{
    uint32_t off = page * XK_PAGE;
    if (off >= L.image_off) {
        if (!image_view) return;
        uint32_t ip = page - (L.image_off >> 12);
        if (ip < img_lo) { img_lo = ip; roll_next = ip; }
        if (!img_listed[ip]) { img_listed[ip] = 1; img_list[img_listed_n++] = ip; }
        return;
    }
    if (off >= phys_limit || slot_of[page] != 0xFFFFu) return;
    if (slots_used >= L.shadow_pages) { slots_overflow++; return; }
    slot_of[page] = (uint16_t)slots_used; slot_page[slots_used++] = page;
}
static void bench(void)
{
    unsigned N = 1u << 20;
    if (L.shadow_pages * XK_PAGE < N) N = L.shadow_pages * XK_PAGE;
    if (L.image_pages * XK_PAGE < N) N = L.image_pages * XK_PAGE;
    if (N < 64u * 1024u) return;
    uint8_t *heap = malloc(2 * N); if (!heap) return;
    uint8_t *a = g_xram + L.shadow_off, *b = g_xram + L.image_copy_off;
    uint64_t t; double mbps[4]; volatile int sink;
    t = xk_os_monotonic_us(); memcpy(a, b, N); mbps[0] = N / 1.048576 / (double)(xk_os_monotonic_us() - t + 1);
    t = xk_os_monotonic_us(); memcpy(heap, b, N); mbps[1] = N / 1.048576 / (double)(xk_os_monotonic_us() - t + 1);
    t = xk_os_monotonic_us(); memcpy(heap + N, heap, N); mbps[2] = N / 1.048576 / (double)(xk_os_monotonic_us() - t + 1);
    t = xk_os_monotonic_us(); sink = memcmp(heap, heap + N, N); mbps[3] = N / 1.048576 / (double)(xk_os_monotonic_us() - t + 1);
    XK_LOG("[render-view] %u KiB throughput MB/s: arena->arena %.0f, arena->heap %.0f, heap->heap %.0f, memcmp heap %.0f (%d)\n", N >> 10, mbps[0], mbps[1], mbps[2], mbps[3], sink);
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
    if ((e = getenv("XV_RENDER_VIEW_PHYS_LIMIT_MIB"))) phys_limit = (uint32_t)atoi(e) << 20;
    if ((e = getenv("XV_RENDER_VIEW_IMAGE"))) image_view = atoi(e) != 0;
    xk_mem_render_view_layout(&L);
    if (!xv_render_view_enabled) { XK_LOG("[render-view] process-start disabled\n"); return; }
    if (!L.shadow_pages || !g_xram) { XK_LOG("[render-view] no shadow region; disabled\n"); xv_render_view_enabled = 0; return; }
    retargets_max = L.shadow_pages * 8;
    slot_of = malloc(L.phys_pages * sizeof *slot_of); slot_page = malloc(L.shadow_pages * sizeof *slot_page);
    img_listed = calloc(L.image_pages, 1); img_list = malloc(L.image_pages * sizeof *img_list);
    sw_slot = calloc(L.shadow_pages, 1); sw_img = calloc(L.image_pages, 1);
    learn_hash = malloc((L.phys_pages + L.image_pages) * sizeof *learn_hash);
    pristine = malloc((size_t)(L.shadow_pages + L.image_pages) * XK_PAGE);
    retargets = malloc(retargets_max * sizeof *retargets);
    if (!slot_of || !slot_page || !img_listed || !img_list || !sw_slot || !sw_img || !learn_hash || !pristine || !retargets) { XK_LOG("[render-view] no memory; disabled\n"); xv_render_view_enabled = 0; return; }
    memset(slot_of, 0xFF, L.phys_pages * sizeof *slot_of);
    memcpy(g_xram + L.image_copy_off, g_xram + L.image_off, L.image_pages * XK_PAGE);
    live_img_base = g_img_base;
    ready = 1;
    bench();
    XK_LOG("[render-view] process-start enabled (in-place); shadow %u pages at %08X for physical pages below %u MiB, image copy %u pages at %08X (%s); learn %u passes every %u frames after a %u us tick gap%s; roll %u, full every %u, copyback %d\n",
           L.shadow_pages, L.shadow_off, phys_limit >> 20, L.image_pages, L.image_copy_off, image_view ? "viewed" : "live", learn_passes, learn_interval, learn_gap_us, learn_armed ? " (armed now)" : "", roll_pages, full_interval, copyback);
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

static void retarget_in_place(void)
{
    retargets_n = 0;
    for (unsigned s = 0; s < slots_used; ++s) {
        uint32_t live = slot_page[s] * XK_PAGE, view = L.shadow_off + s * XK_PAGE, vps[8];
        unsigned n = xk_mem_page_aliases(live, vps, 8);
        if (n > aliases_max) aliases_max = n;
        for (unsigned i = 0; i < n; ++i) {
            if (retargets_n >= retargets_max) { retargets_overflow++; return; }
            retargets[retargets_n++] = (retarget_t){ vps[i], live, view }; g_xpt[vps[i]] = view;
        }
    }
    image_entries = 0;
    if (image_view) for (uint32_t ip = 0; ip < L.image_pages; ++ip) {           /* every image page reads the copy, like X_IMG */
        uint32_t vp = L.image_vpage + ip, live = L.image_off + ip * XK_PAGE;
        if (g_xpt[vp] == live) { g_xpt[vp] = L.image_copy_off + ip * XK_PAGE; image_entries++; }
    }
    if (image_view) xk_mem_set_image_base(g_xram + L.image_copy_off - (L.image_vpage << 12));
}
static void restore_in_place(void)
{
    xk_mem_set_image_base(live_img_base);
    for (uint32_t ip = 0; ip < L.image_pages; ++ip) {
        uint32_t vp = L.image_vpage + ip, view = L.image_copy_off + ip * XK_PAGE;
        if (g_xpt[vp] == view) g_xpt[vp] = L.image_off + ip * XK_PAGE;
    }
    for (unsigned i = 0; i < retargets_n; ++i)
        if (g_xpt[retargets[i].vp] == retargets[i].view) g_xpt[retargets[i].vp] = retargets[i].live;   /* remapped mid-scene: leave it */
    retargets_last = retargets_n; retargets_n = 0;
}
void xv_render_view_enter(unsigned *scope, void *context)
{
    (void)context;
    uint64_t t0 = xk_os_monotonic_us();
    if (!learn_armed && last_leave_us) {
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
        for (unsigned n = 0; n < roll_pages && rolled < L.image_pages - img_lo; ++n) {
            if (roll_next >= L.image_pages) roll_next = img_lo;
            uint32_t ip = roll_next++;
            if (!img_listed[ip]) { memcpy(copy_of_img(ip), live_of_img(ip), XK_PAGE); rolled++; }
        }
    bytes_in += ((uint64_t)slots_used + img_listed_n + rolled) * XK_PAGE;
    retarget_in_place(); bound_since_us = t0; bound = 1;
    enter_us += xk_os_monotonic_us() - t0; frames_entered++; full_frames += full_frame;
}
static int merge_page(const uint8_t *copy, const uint8_t *pre, uint8_t *live)   /* one pass; newlib memcmp is ~86 MB/s here */
{
    const uint32_t *c = (const uint32_t *)copy, *p = (const uint32_t *)pre; uint32_t *l = (uint32_t *)live; unsigned changed = 0;
    for (unsigned i = 0; i < XK_PAGE / 4; i += 4) {
        uint32_t d = (c[i] ^ p[i]) | (c[i+1] ^ p[i+1]) | (c[i+2] ^ p[i+2]) | (c[i+3] ^ p[i+3]);
        if (!d) continue;
        for (unsigned k = i; k < i + 4; ++k) if (c[k] != p[k]) { changed++; if (copyback) l[k] = c[k]; }
    }
    words_merged += changed;
    return changed != 0;
}
static void unbind_merge(void)
{
    restore_in_place(); bound = 0;
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
}
void xv_render_view_leave(unsigned *scope)
{
    uint64_t t0 = xk_os_monotonic_us();
    if (!ready || !depth) { last_leave_us = t0; return; }
    if (--depth || !*scope) { last_leave_us = t0; return; }
    if (bound) unbind_merge();
    last_leave_us = xk_os_monotonic_us();
    leave_us += last_leave_us - t0;
}
/* Remote status poll: a scene bound for over 3 s is stuck (guest polling a word a host writer updates
 * through a live pointer); restore the live mapping so it can continue, and disable the view. Racy by
 * design (another thread), last resort instead of a manual app restart. */
void xv_render_view_watchdog(void)
{
    if (!bound) return;
    uint64_t since = bound_since_us; if (!since || xk_os_monotonic_us() - since < 3000000u) return;
    xv_render_view_enabled = 0; full_frame = 1; unbind_merge(); watchdog_trips++;   /* publish the scene's writes too (a pending request it made) */
    XK_LOG("[render-view] WATCHDOG: scene bound for %llu ms; live mapping restored, view disabled\n", (unsigned long long)((xk_os_monotonic_us() - since) / 1000u));
}
/* In-place mode every fiber sees the view, so a switch needs no action; counted for increment C. */
void xv_render_view_fiber_switch(void) { if (bound) fiber_switches++; }
void xv_render_view_report(unsigned frames)
{
    if (!ready) return;
    XK_LOG("[render-view] %u frames: entered %u (full %u); slots %u (overflow %u) image pages %u listed, tail %u; copy-in %.2f ms/frame (%.0f KiB), merge %.2f ms/frame (%llu words); scene-write set %u slots + %u image pages (%u found late); retargets %u (+%u image, overflow %u), fiber switches in scene %u, remaps in scene %u; aliases max %u\n",
           frames, frames_entered, full_frames, slots_used, slots_overflow, img_listed_n, img_lo < L.image_pages ? L.image_pages - img_lo : 0,
           frames_entered ? (double)enter_us / frames_entered / 1000.0 : 0.0, frames_entered ? (double)bytes_in / frames_entered / 1024.0 : 0.0,
           frames_entered ? (double)leave_us / frames_entered / 1000.0 : 0.0, (unsigned long long)words_merged,
           sw_slots, sw_imgs, sw_found_late, retargets_last, image_entries, retargets_overflow, fiber_switches, mirrors_in_scene, aliases_max);
    frames_entered = full_frames = 0; enter_us = leave_us = bytes_in = words_merged = 0; fiber_switches = mirrors_in_scene = 0;
}
#else
int xv_render_view_enabled;
void xv_render_view_configure(void) {}
void xv_render_view_present(unsigned frame) { (void)frame; }
void xv_render_view_enter(unsigned *scope, void *context) { (void)scope; (void)context; }
void xv_render_view_leave(unsigned *scope) { (void)scope; }
void xv_render_view_report(unsigned frames) { (void)frames; }
void xv_render_view_mirror(uint32_t vpage, uint32_t arena_off) { (void)vpage; (void)arena_off; }
void xv_render_view_fiber_switch(void) {}
void xv_render_view_watchdog(void) {}
#endif
