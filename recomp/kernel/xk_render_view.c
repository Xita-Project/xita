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
 * retarget (listed pages only, image pages too: X_IMG translates through the table under XV_RENDER_VIEW).
 * Exit: restore, merge (copy != pristine -> live). XV_RENDER_VIEW_COPYBACK=0 skips the merge. */
#include "xk.h"
#include "xk_render_view.h"
#include <stdlib.h>
#include <stdio.h>
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
static uint32_t img_lo = UINT32_MAX;
static uint8_t *sw_slot, *sw_img; static unsigned sw_slots, sw_imgs, sw_found_late;
static uint8_t *pristine;                 /* [shadow_pages + image_pages] pages, host memory */
static unsigned full_interval = 30, active_frames;
static uint32_t phys_limit = 4u << 20;      /* shadow only physical pages below this (game state); D3D/audio buffers and guest stacks stay live */
static int image_view = 1;                  /* shadow the image .data tail too */
static volatile uint64_t bound_since_us; static unsigned watchdog_trips;

/* entries retargeted for the current scene: (vpage, live offset, view offset) */
typedef struct { uint32_t vp, live, view; } retarget_t;
static retarget_t *retargets; static unsigned retargets_n, retargets_last, retargets_max, retargets_overflow, image_entries;
/* Thread mode (XV_RENDER_VIEW_THREAD=1, increment C): the retargets go into a private render table that only the
 * thread running the scene binds (host: thread-local pointer, Vita: TPIDRURW); the live table is untouched, so the
 * owner's tick keeps reading and writing live pages while the scene reads its frozen copies. The render table
 * mirrors the live table through xv_render_view_mirror (allocated once, 4 MiB, never copied per frame). */
static int dma_mode, slots_contiguous;   /* XV_RENDER_VIEW_DMA=1 (Vita): one DMAC transfer for the contiguous all-mode slot range instead of a 240 MB/s CPU memcpy per page (perf104: copy-in 20.8 ms/frame on the helper) */
#ifdef __vita__
#include <psp2/kernel/dmac.h>
#endif
static void view_copy(void *dst, const void *src, size_t n)
{
#ifdef __vita__
    if (dma_mode && n >= 65536u) { if (sceDmacMemcpy(dst, src, n) >= 0) return; dma_mode = 0; XK_LOG("[render-view] sceDmacMemcpy failed; CPU copies from now on\n"); }
#endif
    memcpy(dst, src, n);
}
static int all_mode;   /* XV_RENDER_VIEW_ALL: every game-state page listed up front; the view enters before any learning pass */
static int thread_mode; static struct { uint8_t *img_base; uint32_t entries[1u << 20]; } *rt;
#define VIEW_TABLE (thread_mode ? rt->entries : g_xpt)
void xk_os_bind_page_table(uint32_t *table);

static unsigned learn_interval = 150, learn_passes = 6, learn_done, learn_state, learn_next_frame, learn_armed, gap_frames;
static uint32_t learn_gap_us = 8000; static uint32_t *learn_hash; static uint64_t learn_us, last_leave_us;

static unsigned depth, bound, frames_entered, full_frames, fiber_switches, aliases_max, mirrors_in_scene;
static uint64_t enter_us, leave_us, bytes_in, words_merged;
/* Same-frame write conflicts (increment C): a word the scene changed (copy != pristine) whose live copy the tick also
 * changed meanwhile (live != pristine) to a different value. XV_RENDER_VIEW_CONFLICT=1 lets the scene's value
 * win (the original merge), 0 (default) keeps the tick's newer value; either way the sites are counted and the top ones reported. */
static int conflict_scene_wins = 0;   /* default tick wins: scene-wins produced object parent cycles (f_00091A80 recursion) on the host */ static uint64_t conflicts, conflicts_same_value;
#define CONFLICT_SITES 16
static struct { uint32_t page, word; unsigned n; uint32_t live, scene, vaddr; } conflict_sites[CONFLICT_SITES]; static unsigned conflict_nsites, conflict_overflow;
static void conflict_note(uint32_t page, uint32_t word, uint32_t live, uint32_t scene)
{
    conflicts++;
    for (unsigned i = 0; i < conflict_nsites; ++i) if (conflict_sites[i].page == page && conflict_sites[i].word == word) { conflict_sites[i].n++; conflict_sites[i].live = live; conflict_sites[i].scene = scene; return; }
    if (conflict_nsites < CONFLICT_SITES) { uint32_t vps[1]; unsigned n = xk_mem_page_aliases(page * XK_PAGE, vps, 1); conflict_sites[conflict_nsites++] = (typeof(conflict_sites[0])){ page, word, 1, live, scene, n ? (vps[0] << 12) + word : 0 }; } else conflict_overflow++;
}
static int full_frame;

static uint32_t page_hash(const uint8_t *p)
{
    const uint32_t *w = (const uint32_t *)p; uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 1024; i += 4) { h = (h ^ w[i]) * 16777619u; h = (h ^ w[i+1]) * 16777619u; h = (h ^ w[i+2]) * 16777619u; h = (h ^ w[i+3]) * 16777619u; }
    return h;
}
void xv_render_view_mirror(uint32_t vpage, uint32_t arena_off)
{
    if (bound) mirrors_in_scene++;
    if (!thread_mode || !rt) return;
    if (bound) {   /* an entry the scene currently views keeps its copy; restore reads the new live value at leave */
        for (unsigned i = 0; i < retargets_n; ++i) if (retargets[i].vp == vpage) return;
        uint32_t cur = rt->entries[vpage]; if (cur >= L.image_copy_off && cur < L.image_copy_off + L.image_pages * XK_PAGE) return;
    }
    rt->entries[vpage] = arena_off;
}
static void learn_page(uint32_t page)
{
    uint32_t off = page * XK_PAGE;
    if (off >= L.image_off) {
        if (!image_view) return;
        uint32_t ip = page - (L.image_off >> 12);
        if (ip < img_lo) img_lo = ip;
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
    if ((e = getenv("XV_RENDER_VIEW_FULL_INTERVAL"))) full_interval = (unsigned)atoi(e);
    if ((e = getenv("XV_RENDER_VIEW_PHYS_LIMIT_MIB"))) phys_limit = (uint32_t)atoi(e) << 20;
    if ((e = getenv("XV_RENDER_VIEW_IMAGE"))) image_view = atoi(e) != 0;
    if ((e = getenv("XV_RENDER_VIEW_THREAD"))) thread_mode = atoi(e) != 0;
    if ((e = getenv("XV_RENDER_VIEW_CONFLICT"))) conflict_scene_wins = atoi(e) != 0;
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
    if (thread_mode) {
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
        rt = malloc(sizeof *rt);
        if (!rt) { XK_LOG("[render-view] no render table; thread mode off\n"); thread_mode = 0; }
        else { memcpy(rt->entries, g_xpt, sizeof rt->entries); rt->img_base = ((uint8_t *const *)g_xpt)[-1]; XK_LOG("[render-view] thread mode: private render table, bound by the scene thread only\n"); }
#else
        XK_LOG("[render-view] thread mode needs XV_THREAD_PAGE_TABLE; off\n"); thread_mode = 0;
#endif
    }
    ready = 1;
    bench();
    {   /* XV_RENDER_VIEW_ALL=1: list every physical page below the limit up front (2: the image .data pages too), so the
         * scene reads a frozen copy of the whole game state instead of the learned subset. The learned set missed the
         * corridor's object churn (Pi ov2b, 2026-09-22: 7 slots learned in the cinematic; tick N+1 tore an object's model
         * reference under real overlap and the scene looped forever in A26B0). Costs one copy-in of the listed pages per
         * frame; learning passes are skipped. */
        const char *e = getenv("XV_RENDER_VIEW_ALL"); int all = e ? atoi(e) : 0; all_mode = all;
        if (all > 0) {
            unsigned n = phys_limit / XK_PAGE; if (n > L.phys_pages) n = L.phys_pages;
            for (unsigned i = 0; i < n; ++i) learn_page(i);
            if (all >= 2 && image_view) for (unsigned i = 0; i < L.image_pages; ++i) learn_page((L.image_off >> 12) + i);
            { const char *d = getenv("XV_RENDER_VIEW_DMA"); dma_mode = d ? atoi(d) : 0; }
            slots_contiguous = 1; for (unsigned i = 0; i < slots_used; ++i) if (slot_page[i] != i) { slots_contiguous = 0; break; }
            if (dma_mode) XK_LOG("[render-view] DMA copy-in: %s\n", slots_contiguous ? "one transfer for the contiguous slot range" : "slots not contiguous, per-page copies");
            if (all >= 2) { learn_done = learn_passes; learn_armed = 0; }   /* 1: the learning passes still list the image .data pages the tick changes */
            XK_LOG("[render-view] all mode %d: %u shadow slots (%u overflow), %u image pages listed; learning %s\n", all, slots_used, slots_overflow, img_listed_n, all >= 2 ? "skipped" : "image pages only");
        }
    }
    XK_LOG("[render-view] process-start enabled (in-place); shadow %u pages at %08X for physical pages below %u MiB, image copy %u pages at %08X (%s); learn %u passes every %u frames after a %u us tick gap%s; full every %u, copyback %d\n",
           L.shadow_pages, L.shadow_off, phys_limit >> 20, L.image_pages, L.image_copy_off, image_view ? "viewed" : "live", learn_passes, learn_interval, learn_gap_us, learn_armed ? " (armed now)" : "", full_interval, copyback);
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

/* Halo's cache-file request table (global 2E2D24 -> +34: entries; the spin at 32B00 polls it): is it
 * shadowed?  Logged once when the view activates and on a watchdog trip (handoff 20260920 §29a). */
static void log_request_table(const char *when)
{
    uint32_t tbl = *(const uint32_t *)(g_img_base + 0x2E2D24u), req = 0, off = 0; unsigned slot = 0xFFFFu;
    if (tbl) { req = *(const uint32_t *)((const uint8_t *)X_G(tbl + 0x34u)); if (req) { off = X_PT[req >> 12]; if (off < L.image_off) slot = slot_of[off >> 12]; } }
    XK_LOG("[render-view] %s: request table %08X entries %08X -> arena %08X slot %s%u\n", when, tbl, req, off, slot == 0xFFFFu ? "none " : "#", slot == 0xFFFFu ? 0 : slot);
}
static void retarget_in_place(void)
{
    retargets_n = 0;
    for (unsigned s = 0; s < slots_used; ++s) {
        uint32_t live = slot_page[s] * XK_PAGE, view = L.shadow_off + s * XK_PAGE, vps[8];
        unsigned n = xk_mem_page_aliases(live, vps, 8);
        if (n > aliases_max) aliases_max = n;
        for (unsigned i = 0; i < n; ++i) {
            if (retargets_n >= retargets_max) { retargets_overflow++; return; }
            retargets[retargets_n++] = (retarget_t){ vps[i], live, view }; VIEW_TABLE[vps[i]] = view;
        }
    }
    image_entries = 0;
    for (unsigned i = 0; i < img_listed_n; ++i) {                 /* listed image pages only (X_IMG goes through the table) */
        uint32_t ip = img_list[i], vp = L.image_vpage + ip, live = L.image_off + ip * XK_PAGE;
        if (VIEW_TABLE[vp] == live) { VIEW_TABLE[vp] = L.image_copy_off + ip * XK_PAGE; image_entries++; }
    }
}
static void restore_in_place(void)
{
    for (unsigned i = 0; i < img_listed_n; ++i) {
        uint32_t ip = img_list[i], vp = L.image_vpage + ip, view = L.image_copy_off + ip * XK_PAGE;
        if (VIEW_TABLE[vp] == view) VIEW_TABLE[vp] = thread_mode ? g_xpt[vp] : L.image_off + ip * XK_PAGE;   /* thread mode: whatever live holds now */
    }
    for (unsigned i = 0; i < retargets_n; ++i)
        if (VIEW_TABLE[retargets[i].vp] == retargets[i].view) VIEW_TABLE[retargets[i].vp] = thread_mode ? g_xpt[retargets[i].vp] : retargets[i].live;   /* remapped mid-scene: leave it */
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
    if (!learn_done && !all_mode) return;   /* all mode: the listed set is complete from the start (Vita perf103: entered 0 while waiting for the passes) */
    *scope = 1;
    full_frame = active_frames < 30 || (full_interval && active_frames % full_interval == 0);
    active_frames++;
    if (all_mode && slots_contiguous && slots_used) {   /* all mode: slots 0..n-1 = physical pages 0..n-1: one range copy (DMA on the Vita) */
        view_copy(shadow_of(0), live_of_slot(0), (size_t)slots_used * XK_PAGE);
        if (full_frame) view_copy(pristine_slot(0), shadow_of(0), (size_t)slots_used * XK_PAGE);
        else for (unsigned s = 0; s < slots_used; ++s) if (sw_slot[s]) memcpy(pristine_slot(s), shadow_of(s), XK_PAGE);
    } else
    for (unsigned s = 0; s < slots_used; ++s) {
        memcpy(shadow_of(s), live_of_slot(s), XK_PAGE);
        if (full_frame || sw_slot[s]) memcpy(pristine_slot(s), shadow_of(s), XK_PAGE);
    }
    for (unsigned i = 0; i < img_listed_n; ++i) {
        uint32_t ip = img_list[i];
        memcpy(copy_of_img(ip), live_of_img(ip), XK_PAGE);
        if (full_frame || sw_img[ip]) memcpy(pristine_img(ip), copy_of_img(ip), XK_PAGE);
    }
    bytes_in += ((uint64_t)slots_used + img_listed_n) * XK_PAGE;
    if (active_frames == 1) log_request_table("first viewed frame");
    retarget_in_place(); bound_since_us = t0; bound = 1;
    if (thread_mode) xk_os_bind_page_table(rt->entries);       /* this thread (the one running the body) sees the view */
    { extern void xv_write_watch_arm(void) __attribute__((weak)); if (xv_write_watch_arm) xv_write_watch_arm(); }   /* host diagnostic (write_watch.c) */
    enter_us += xk_os_monotonic_us() - t0; frames_entered++; full_frames += full_frame;
}
static int merge_page(const uint8_t *copy, const uint8_t *pre, uint8_t *live, uint32_t page_id)   /* one pass; newlib memcmp is ~86 MB/s here */
{
    const uint32_t *c = (const uint32_t *)copy, *p = (const uint32_t *)pre; uint32_t *l = (uint32_t *)live; unsigned changed = 0;
    for (unsigned i = 0; i < XK_PAGE / 4; i += 4) {
        uint32_t d = (c[i] ^ p[i]) | (c[i+1] ^ p[i+1]) | (c[i+2] ^ p[i+2]) | (c[i+3] ^ p[i+3]);
        if (!d) continue;
        for (unsigned k = i; k < i + 4; ++k) if (c[k] != p[k]) {
            changed++;
            uint32_t lv = l[k];
            if (lv != p[k]) {   /* the tick wrote this word during the scene */
                if (lv == c[k]) { conflicts_same_value++; continue; }
                conflict_note(page_id, k * 4u, lv, c[k]);
                if (!conflict_scene_wins) continue;
            }
            if (copyback) l[k] = c[k];
        }
    }
    words_merged += changed;
    return changed != 0;
}
static void unbind_merge(void)
{
    restore_in_place(); bound = 0;
    for (unsigned s = 0; s < slots_used; ++s) {
        if (!full_frame && !sw_slot[s]) continue;
        if (!merge_page(shadow_of(s), pristine_slot(s), live_of_slot(s), slot_page[s])) continue;
        if (!sw_slot[s]) { sw_slot[s] = 1; sw_slots++; if (active_frames > 30) sw_found_late++; }
    }
    for (unsigned i = 0; i < img_listed_n; ++i) {
        uint32_t ip = img_list[i];
        if (!full_frame && !sw_img[ip]) continue;
        if (!merge_page(copy_of_img(ip), pristine_img(ip), live_of_img(ip), (L.image_off >> 12) + ip)) continue;
        if (!sw_img[ip]) { sw_img[ip] = 1; sw_imgs++; if (active_frames > 30) sw_found_late++; }
    }
}
void xv_render_view_leave(unsigned *scope)
{
    uint64_t t0 = xk_os_monotonic_us();
    if (!ready || !depth) { last_leave_us = t0; return; }
    if (--depth || !*scope) { last_leave_us = t0; return; }
    if (bound) { if (thread_mode) xk_os_bind_page_table(g_xpt); unbind_merge(); }
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
    log_request_table("watchdog");
    xv_render_view_enabled = 0; full_frame = 1; unbind_merge(); watchdog_trips++;   /* publish the scene's writes too (a pending request it made) */
    XK_LOG("[render-view] WATCHDOG: scene bound for %llu ms; live mapping restored, view disabled\n", (unsigned long long)((xk_os_monotonic_us() - since) / 1000u));
}
/* In-place mode every fiber sees the view, so a switch needs no action; counted for increment C. */
void xv_render_view_fiber_switch(void) { if (bound) fiber_switches++; }
uint8_t *xv_render_view_shadow_of_phys(uint32_t phys_page) { uint32_t s = ready && phys_page < L.phys_pages ? slot_of[phys_page] : 0xFFFFFFFFu; return s == 0xFFFFFFFFu || s >= slots_used ? NULL : shadow_of(s); }
void xv_render_view_report(unsigned frames)
{
    if (!ready) return;
    XK_LOG("[render-view] %u frames: entered %u (full %u); slots %u (overflow %u) image pages %u listed, tail %u; copy-in %.2f ms/frame (%.0f KiB), merge %.2f ms/frame (%llu words); scene-write set %u slots + %u image pages (%u found late); retargets %u (+%u image, overflow %u), fiber switches in scene %u, remaps in scene %u; aliases max %u\n",
           frames, frames_entered, full_frames, slots_used, slots_overflow, img_listed_n, img_lo < L.image_pages ? L.image_pages - img_lo : 0,
           frames_entered ? (double)enter_us / frames_entered / 1000.0 : 0.0, frames_entered ? (double)bytes_in / frames_entered / 1024.0 : 0.0,
           frames_entered ? (double)leave_us / frames_entered / 1000.0 : 0.0, (unsigned long long)words_merged,
           sw_slots, sw_imgs, sw_found_late, retargets_last, image_entries, retargets_overflow, fiber_switches, mirrors_in_scene, aliases_max);
    frames_entered = full_frames = 0; enter_us = leave_us = bytes_in = words_merged = 0; fiber_switches = mirrors_in_scene = 0;
    if (conflicts || conflicts_same_value) {
        char line[400]; int ln = snprintf(line, sizeof line, "[render-view-conflicts] %u frames: %llu conflicting words (%llu same-value), policy %s; sites (phys-page:word n live/scene):",
                                          frames, (unsigned long long)conflicts, (unsigned long long)conflicts_same_value, conflict_scene_wins ? "scene wins" : "tick wins");
        for (unsigned i = 0; i < conflict_nsites && ln < 360; ++i) ln += snprintf(line + ln, sizeof line - ln, " %X:%X=%08X %u %X/%X", conflict_sites[i].page, conflict_sites[i].word, conflict_sites[i].vaddr, conflict_sites[i].n, conflict_sites[i].live, conflict_sites[i].scene);
        if (conflict_overflow) ln += snprintf(line + ln, sizeof line - ln, " (+%u)", conflict_overflow);
        XK_LOG("%s\n", line); conflicts = conflicts_same_value = 0; conflict_nsites = 0; conflict_overflow = 0;
    }
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
