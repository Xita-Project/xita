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
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#endif
static int clib_copy = -1;   /* XV_RENDER_VIEW_CLIB=1: sceClibMemcpy (NEON) for the CPU copies; newlib memcpy measured ~216 MB/s here */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif
static int pld_copy = -1;    /* XV_RENDER_VIEW_PLD=1: NEON 64-byte copy with loads requested 512 bytes ahead (page runs are 4 KiB multiples) */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static void copy_pld(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst; const uint8_t *s = src; size_t i = 0;
    for (; i + 64 <= n; i += 64) {
        __builtin_prefetch(s + i + 512);
        uint8x16_t a = vld1q_u8(s + i), b = vld1q_u8(s + i + 16), c = vld1q_u8(s + i + 32), e = vld1q_u8(s + i + 48);
        vst1q_u8(d + i, a); vst1q_u8(d + i + 16, b); vst1q_u8(d + i + 32, c); vst1q_u8(d + i + 48, e);
    }
    if (i < n) memcpy(d + i, s + i, n - i);
}
#endif
static void view_copy(void *dst, const void *src, size_t n)
{
#ifdef __vita__
    if (dma_mode && n >= 65536u) { if (sceDmacMemcpy(dst, src, n) >= 0) return; dma_mode = 0; XK_LOG("[render-view] sceDmacMemcpy failed; CPU copies from now on\n"); }
    if (pld_copy < 0) { const char *e = getenv("XV_RENDER_VIEW_PLD"); pld_copy = e ? atoi(e) != 0 : 0; }
    if (pld_copy) { copy_pld(dst, src, n); return; }
    if (clib_copy < 0) { const char *e = getenv("XV_RENDER_VIEW_CLIB"); clib_copy = e ? atoi(e) != 0 : 0; }
    if (clib_copy) { sceClibMemcpy(dst, src, n); return; }
#endif
    memcpy(dst, src, n);
}
static const char *array_words = "cluster,list";   /* XV_RENDER_VIEW_ARRAYS (mode 4): the cluster/object reference arrays + the object list headers/refs. NOT "object": that also freezes "cached object render states", whose scene-side writes lose the tick-wins merge and objects vanish (Vita perf112: 54 draw batches instead of 187, NPCs flickering) */
static int all_mode;   /* XV_RENDER_VIEW_ALL: every game-state page listed up front; the view enters before any learning pass */
static int thread_mode; static struct { uint8_t *img_base; uint32_t entries[1u << 20]; } *rt;
#define VIEW_TABLE (thread_mode ? rt->entries : g_xpt)
void xk_os_bind_page_table(uint32_t *table);

static unsigned learn_interval = 150, learn_passes = 6, learn_done, learn_state, learn_next_frame, learn_armed, gap_frames;
static uint32_t learn_gap_us = 8000; static uint32_t *learn_hash; static uint64_t learn_us, last_leave_us;

static unsigned depth, bound, frames_entered, full_frames, fiber_switches, aliases_max, mirrors_in_scene;
static uint64_t prepare_us;   /* owner-side copy-in (xv_render_view_prepare) */
static unsigned copy_runs;   /* mode 3/4: contiguous live-page runs copied per window */
/* Early snapshot (XV_RENDER_VIEW_EARLY=1, all mode >= 3): two shadow banks of shadow_pages/2 slots. While the owner waits
 * for scene N (tick N+1 complete) it copies the next snapshot into the idle bank; after the join it re-copies the few
 * pages scene N's merge wrote back, copies slots the listing appended, and swaps banks. The copy leaves the frame's
 * serialized path (owner joined, helper idle) for the owner's wait. */
static unsigned cur_bank, bank_pages; static int early_mode;
static int early_valid; static unsigned early_bank, early_slots, early_copies, early_fallbacks, early_recopied; static uint64_t early_us;
static int split_idle = -1; static unsigned early_split;   /* XV_RENDER_VIEW_EARLY_SPLIT (default 1): split the early copy when the helper is idle */
static uint8_t *merged_slot;   /* [shadow_pages] slots scene N's merge copied back into live */
#define ASSIST_RUNS_MAX 1024
#ifndef __vita__
#define ASSIST_MAX 1
#endif
static uint64_t enter_us, leave_us, bytes_in, words_merged;
static uint64_t leave_bind_us, leave_restore_us, leave_merge_us; static unsigned merged_pages_compared;   /* leave split for the report */
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
    if (slots_used >= (bank_pages ? bank_pages : L.shadow_pages)) { slots_overflow++; return; }
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
        rt = NULL;
#ifdef __vita__
        {   /* XV_RENDER_VIEW_RT_PHYCONT (default 1): the scene helper translates every guest access through this 4 MiB
             * table; physically contiguous memory gives it one stable cache/TLB layout (identical builds swung the
             * helper's scene 53 <-> 58.5 ms between launches, perf164/164b). Falls back to the heap. */
            const char *e = getenv("XV_RENDER_VIEW_RT_PHYCONT");
            if (!e || atoi(e)) {
                SceUID uid = sceKernelAllocMemBlock("xv_rv_table", SCE_KERNEL_MEMBLOCK_TYPE_USER_MAIN_PHYCONT_RW, 5u << 20, NULL);
                void *base = NULL;
                if (uid >= 0 && sceKernelGetMemBlockBase(uid, &base) >= 0 && base) rt = base;
                XK_LOG("[render-view] render table: physically contiguous block %s (uid %08X)\n", rt ? "ok" : "refused, heap", (unsigned)uid);
            }
        }
#endif
        if (!rt) rt = malloc(sizeof *rt);
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
            { const char *ee = getenv("XV_RENDER_VIEW_EARLY"); early_mode = ee && atoi(ee) && all >= 3 && L.shadow_pages >= 256;
              { const char *es = getenv("XV_RENDER_VIEW_EARLY_SPLIT"); split_idle = !es || atoi(es) != 0; } if (early_mode) { bank_pages = L.shadow_pages / 2; merged_slot = calloc(L.shadow_pages, 1); if (!merged_slot) { early_mode = 0; bank_pages = 0; } XK_LOG("[render-view] early snapshot: %s, 2 banks of %u slots\n", early_mode ? "on" : "off (alloc)", bank_pages); } }
        { const char *w = getenv("XV_RENDER_VIEW_ARRAYS"); if (w && *w) array_words = w; }
        if (all > 0) {
            unsigned n = phys_limit / XK_PAGE; if (n > L.phys_pages) n = L.phys_pages;
            if (all >= 3) n = 0;   /* object pool only (4: + named data arrays): listed at the first gameplay enter (list_object_pool), so kernel-written completion words elsewhere stay live (perf104/105 hung: the scene polled a shadowed word) */
            for (unsigned i = 0; i < n; ++i) learn_page(i);
            if (all == 2 && image_view) for (unsigned i = 0; i < L.image_pages; ++i) learn_page((L.image_off >> 12) + i);   /* mode 3 listed all 933 image pages by mistake (perf107: copy-in still 4.5 MB) */
            { const char *d = getenv("XV_RENDER_VIEW_DMA"); dma_mode = d ? atoi(d) : 0; }
            slots_contiguous = 1; for (unsigned i = 1; i < slots_used; ++i) if (slot_page[i] != slot_page[0] + i) { slots_contiguous = 0; break; }
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
static inline uint8_t *shadow_bank(unsigned b, unsigned s) { return g_xram + L.shadow_off + ((size_t)b * bank_pages + s) * XK_PAGE; }
static inline uint8_t *shadow_of(unsigned s) { return shadow_bank(cur_bank, s); }
static inline uint8_t *live_of_slot(unsigned s) { return g_xram + (size_t)slot_page[s] * XK_PAGE; }
static inline uint8_t *copy_of_img(uint32_t ip) { return g_xram + L.image_copy_off + (size_t)ip * XK_PAGE; }
static inline uint8_t *live_of_img(uint32_t ip) { return g_xram + L.image_off + (size_t)ip * XK_PAGE; }
static inline uint8_t *pristine_slot(unsigned s) { return pristine + ((size_t)cur_bank * bank_pages + s) * XK_PAGE; }
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
        uint32_t live = slot_page[s] * XK_PAGE, view = L.shadow_off + (cur_bank * bank_pages + s) * XK_PAGE, vps[8];
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
/* All mode 3: the object header table and the physical page range spanned by the live objects' data (Halo allocates
 * them from one pool), read through the LIVE table (this runs before the view is bound). Once per map (table changes). */
/* Halo data arrays in the physical game-state region: header = name[32], max u16 @0x20, element size u16 @0x22,
 * signature 'd@t@' @0x28, data pointer @0x34 (the "object" table at 800B9370 has its signature at 800B938C). Logged
 * once per map; mode 4 freezes the ones whose name contains a word of XV_RENDER_VIEW_ARRAYS (default
 * "object,cluster"): the render list source that went stale under overlap (core.4415: a deleted object's datum in the
 * list, its header slot already 0 in the snapshot) lives in the cluster/object reference arrays, not in the pool. */
static int name_matches(const char *name)
{
    const char *w = array_words;
    while (*w) { const char *e = w; while (*e && *e != ',') e++; size_t n = (size_t)(e - w);
        for (const char *p = name; *p; ++p) if (strncmp(p, w, n) == 0) return 1;
        w = *e ? e + 1 : e; }
    return 0;
}
static void list_data_arrays(int freeze)
{
    unsigned n = 0, frozen = 0; char line[420]; int ln = 0;
    for (uint32_t off = 0x28u; off + 0x38u <= phys_limit; off += 4u) {
        const uint8_t *h = g_xram + off - 0x28u; uint32_t sig; memcpy(&sig, g_xram + off, 4); if (sig != 0x64407440u) continue;
        uint16_t max, esz; memcpy(&max, h + 0x20, 2); memcpy(&esz, h + 0x22, 2); uint32_t data; memcpy(&data, h + 0x34, 4);
        char name[33]; memcpy(name, h, 32); name[32] = 0; for (int i = 0; i < 32 && name[i]; ++i) if (name[i] < 32 || name[i] > 126) name[i] = '?';
        int hit = freeze && name_matches(name); uint32_t hdr_va = 0x80000000u + off - 0x28u;
        if (ln > 300) { XK_LOG("[render-view] arrays:%s\n", line); ln = 0; }
        ln += snprintf(line + ln, sizeof line - ln, " |%s@%08X %u*%u->%08X%s", name, hdr_va, max, esz, data, hit ? " FROZEN" : "");
        n++;
        if (hit) {
            frozen++;
            for (uint32_t va = hdr_va & ~0xFFFu; va < hdr_va + 0x38u; va += XK_PAGE) { uint32_t o = g_xpt[va >> 12]; if (o < phys_limit) learn_page(o >> 12); }
            if (data >= 0x80000000u && max && esz) for (uint32_t va = data & ~0xFFFu; va < data + (uint32_t)max * esz; va += XK_PAGE) { uint32_t o = g_xpt[va >> 12]; if (o < phys_limit) learn_page(o >> 12); }
        }
    }
    if (ln) XK_LOG("[render-view] arrays:%s\n", line);
    /* The frozen "cluster ... reference" chains start from per-cluster head tables that are plain 512-entry allocations
     * (0x800 bytes right below each array), not data arrays, so the name match missed them: the scene followed LIVE heads
     * the tick had just relinked into FROZEN chains and lost moving objects for a frame (Vita perf179 XV_OBJTRACE: the
     * a10 crewmen, tag E31701A3, uncollected in ~5% of scenes; only under the overlap). XV_RENDER_VIEW_HEADS lists the
     * image globals holding the table pointers: 2FC6A0 collideable objects, 2FC690 noncollideable objects, 2FC670 lights. */
    if (freeze) {
        const char *hs = getenv("XV_RENDER_VIEW_HEADS"); if (!hs) hs = "2FC670,2FC690,2FC6A0";
        unsigned nh = 0; char hl[200]; int hn = 0; hl[0] = 0;
        for (const char *p = hs; *p; ) {
            char *q; uint32_t g = (uint32_t)strtoul(p, &q, 16); if (q == p) break; p = q; while (*p == ',' || *p == ' ') p++;
            uint32_t t = g ? X_IMG32(g) : 0;
            if (t >= 0x80000000u) { for (uint32_t va = t & ~0xFFFu; va < t + 0x800u; va += XK_PAGE) { uint32_t o = g_xpt[va >> 12]; if (o < phys_limit) learn_page(o >> 12); }
                nh++; if (hn < (int)sizeof hl - 24) hn += snprintf(hl + hn, sizeof hl - hn, " %X->%08X", g, t); }
        }
        XK_LOG("[render-view] %u cluster head tables frozen:%s; slots %u\n", nh, nh ? hl : " none", slots_used);
    }
    XK_LOG("[render-view] %u data arrays below %u MiB, %u frozen by name (%s); slots %u\n", n, phys_limit >> 20, frozen, array_words, slots_used);
}
static uint32_t pool_table_seen, pool_lo = 0xFFFFFFFFu, pool_hi;
static void list_object_pool(void)
{
    uint32_t table = X_IMG32(0x2FC6ACu); if (!table) return;
    uint32_t base = X_M32(table + 0x34u); unsigned max = X_M16(table + 0x20u); if (!base || !max || max > 4096u) return;
    if (table != pool_table_seen) { pool_table_seen = table; pool_lo = 0xFFFFFFFFu; pool_hi = 0; list_data_arrays(all_mode == 4); }
    uint32_t lo = pool_lo, hi = pool_hi; unsigned live = 0;
    for (unsigned i = 0; i < max; ++i) { uint32_t e = base + i * 12u; if (!X_M16(e) || !X_M32(e + 8u)) continue; uint32_t o = X_M32(e + 8u); if (o < lo) lo = o; if (o + 0x1000u > hi) hi = o + 0x1000u; live++; }
    if (!live || (lo == pool_lo && hi == pool_hi)) return;   /* objects spawn as the level runs: the range grows, the listing follows it (every enter) */
    unsigned before = slots_used;
    for (uint32_t va = table & ~0xFFFu; va < base + max * 12u; va += XK_PAGE) { uint32_t off = g_xpt[va >> 12]; if (off < phys_limit) learn_page(off >> 12); }
    for (uint32_t va = lo & ~0xFFFu; va < hi; va += XK_PAGE) { uint32_t off = g_xpt[va >> 12]; if (off < phys_limit) learn_page(off >> 12); }
    pool_lo = lo; pool_hi = hi;
    slots_contiguous = 1; for (unsigned i = 1; i < slots_used; ++i) if (slot_page[i] != slot_page[0] + i) { slots_contiguous = 0; break; }
    XK_LOG("[render-view] object pool: table %08X, %u live objects, data %08X..%08X: +%u slots (%u, %s)\n", table, live, lo, hi, slots_used - before, slots_used, slots_contiguous ? "contiguous" : "not contiguous");
}
/* The copy-in, as its own step: the OWNER runs it before dispatching an overlapped scene (xk_scene_thread.c), so the
 * snapshot is taken while nothing mutates the pages. Done on the helper after dispatch (perf104-108, Pi all1a-all3a)
 * the copy raced tick N+1 and the snapshot itself was torn: the same bogus model pointer (01914660) hung A26B0 in
 * every Pi run, frozen pages or not. */
static int prepared;
static uint64_t prep_list_us, prep_copy_us, prep_pristine_us;   /* copy_in split, for the report */
/* Split snapshot copy (XV_RENDER_VIEW_SPLIT=1, Vita): the owner's copy-in sits on the frame's serialized path (owner
 * joined scene N, helper idle until scene N+1 is dispatched). A copy thread on the scene helper's core takes the
 * second half of the page runs while the owner copies the first; the owner waits for it before dispatching. */
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#define ASSIST_MAX 512
/* Two assistants: [0] on the scene helper's core (XV_RENDER_VIEW_SPLIT_CORE, default 1; only when that core is free),
 * [1] on core 0 with the render pump (XV_RENDER_VIEW_SPLIT_CORE0=1). A single core's memcpy is latency-bound (~230 MB/s)
 * far below the bus, so each share runs close to full speed. */
typedef struct { unsigned n; uint8_t *dst[ASSIST_MAX]; const uint8_t *src[ASSIST_MAX]; uint32_t bytes[ASSIST_MAX]; SceUID go, done; int state; } assist_t;
static assist_t assists[2] = { { .go = -1, .done = -1, .state = -1 }, { .go = -1, .done = -1, .state = -1 } };
static unsigned assist_splits, assist_three;
static int assist_main(SceSize args, void *argp)
{
    (void)args; assist_t *a = *(assist_t **)argp;
    for (;;) {
        if (sceKernelWaitSema(a->go, 1, NULL) < 0) return 0;
        for (unsigned i = 0; i < a->n; ++i) view_copy(a->dst[i], a->src[i], a->bytes[i]);
        sceKernelSignalSema(a->done, 1);
    }
}
static int assist_start(unsigned i)
{
    assist_t *a = &assists[i];
    if (a->state >= 0) return a->state;
    a->state = 0;
    const char *e = getenv("XV_RENDER_VIEW_SPLIT"); if (!(e && atoi(e))) return 0;
    int core = 0;
    if (i == 0) { const char *ce = getenv("XV_RENDER_VIEW_SPLIT_CORE"); core = ce ? atoi(ce) : 1; }
    else { const char *ce = getenv("XV_RENDER_VIEW_SPLIT_CORE0"); if (!(ce && atoi(ce))) return 0; }
    int mask = core == 0 ? SCE_KERNEL_CPU_MASK_USER_0 : core == 2 ? SCE_KERNEL_CPU_MASK_USER_2 : SCE_KERNEL_CPU_MASK_USER_1;
    a->go = sceKernelCreateSema(i ? "xv_rv_assist0_go" : "xv_rv_assist_go", 0, 0, 1, NULL);
    a->done = sceKernelCreateSema(i ? "xv_rv_assist0_done" : "xv_rv_assist_done", 0, 0, 1, NULL);
    SceUID t = a->go >= 0 && a->done >= 0 ? sceKernelCreateThread(i ? "xv_rv_assist0" : "xv_rv_assist", assist_main, sceKernelGetThreadCurrentPriority(), 16 * 1024, 0, mask, NULL) : -1;
    if (t >= 0) { assist_t *arg = a; if (sceKernelStartThread(t, sizeof arg, &arg) >= 0) a->state = 1; }
    XK_LOG("[render-view] split copy assistant %u %s (core %d)\n", i, a->state ? "on" : "FAILED", core);
    return a->state;
}
static int assist_ready(void) { return assist_start(0); }
#endif
/* allowed: bit 0 = the helper-core assistant may take a share (its core is idle), bit 1 = the core-0 assistant may. */
static void copy_runs_to(unsigned bank, int allowed)
{
    /* collect the runs of consecutive live pages; shadow slots are contiguous by slot index */
    static unsigned run_s[ASSIST_RUNS_MAX], run_n[ASSIST_RUNS_MAX]; unsigned runs = 0;
    for (unsigned s = 0; s < slots_used; ) {
        unsigned e = s + 1;
        while (e < slots_used && slot_page[e] == slot_page[e - 1] + 1u) e++;
        if (runs < ASSIST_RUNS_MAX) { run_s[runs] = s; run_n[runs] = e - s; runs++; }
        else view_copy(shadow_bank(bank, s), live_of_slot(s), (size_t)(e - s) * XK_PAGE);
        s = e;
    }
    copy_runs += runs;
#ifdef __vita__
    assist_t *use[2]; unsigned k = 0;
    if (slots_used >= 32) {
        if ((allowed & 1) && assist_start(0)) use[k++] = &assists[0];
        if ((allowed & 2) && assist_start(1)) use[k++] = &assists[1];
    }
    if (k) {
        /* pages [0, slots_used) in k+1 equal shares by slot order: share 0 the owner's, share j assistant j-1's */
        unsigned shares = k + 1, owner_end = slots_used / shares;
        for (unsigned j = 0; j < k; ++j) use[j]->n = 0;
        static unsigned own_s[ASSIST_RUNS_MAX + 4], own_n[ASSIST_RUNS_MAX + 4]; unsigned owns = 0;
        for (unsigned r = 0; r < runs; ++r) {
            unsigned a = run_s[r], b = run_s[r] + run_n[r];
            for (unsigned w = 0; w < shares && a < b; ++w) {
                unsigned lo = slots_used * w / shares, hi = w + 1 == shares ? slots_used : slots_used * (w + 1) / shares;
                unsigned x = a > lo ? a : lo, y = b < hi ? b : hi;
                if (x >= y) continue;
                if (w == 0) { own_s[owns] = x; own_n[owns] = y - x; owns++; }
                else {
                    assist_t *as = use[w - 1];
                    if (as->n < ASSIST_MAX) { as->dst[as->n] = shadow_bank(bank, x); as->src[as->n] = live_of_slot(x); as->bytes[as->n] = (y - x) * XK_PAGE; as->n++; }
                    else view_copy(shadow_bank(bank, x), live_of_slot(x), (size_t)(y - x) * XK_PAGE);
                }
                a = y;
            }
        }
        (void)owner_end;
        for (unsigned j = 0; j < k; ++j) sceKernelSignalSema(use[j]->go, 1);
        for (unsigned o = 0; o < owns; ++o) view_copy(shadow_bank(bank, own_s[o]), live_of_slot(own_s[o]), (size_t)own_n[o] * XK_PAGE);
        for (unsigned j = 0; j < k; ++j) sceKernelWaitSema(use[j]->done, 1, NULL);
        assist_splits++; assist_three += k == 2;
        return;
    }
#else
    (void)allowed;
#endif
    for (unsigned r = 0; r < runs; ++r) view_copy(shadow_bank(bank, run_s[r]), live_of_slot(run_s[r]), (size_t)run_n[r] * XK_PAGE);
}
static void copy_in_runs(int from_owner) { copy_runs_to(cur_bank, from_owner ? 3 : 0); }
static void copy_in(int from_owner)
{
    uint64_t t0 = xk_os_monotonic_us();
    if (all_mode >= 3) list_object_pool();
    uint64_t t1 = xk_os_monotonic_us(); prep_list_us += t1 - t0;
    full_frame = active_frames < 30 || (full_interval && active_frames % full_interval == 0);
    active_frames++;
    if (all_mode && slots_contiguous && slots_used) {   /* all mode: an ascending slot range = one physical range: one range copy (DMA on the Vita) */
        view_copy(shadow_of(0), live_of_slot(0), (size_t)slots_used * XK_PAGE);
        uint64_t t2 = xk_os_monotonic_us(); prep_copy_us += t2 - t1;
        if (full_frame) view_copy(pristine_slot(0), shadow_of(0), (size_t)slots_used * XK_PAGE);
        else for (unsigned s = 0; s < slots_used; ++s) if (sw_slot[s]) memcpy(pristine_slot(s), shadow_of(s), XK_PAGE);
        prep_pristine_us += xk_os_monotonic_us() - t2;
    } else {
        /* mode 3/4: the listed slots are scattered pages, but mostly ascending runs of the object pool and data arrays;
         * copy each run of consecutive live pages in one call (DMA for runs of 64 KiB and up). This branch was the
         * owner's unattributed ~4.2 ms per frame (232 pages, one memcpy each, perf145). */
        copy_in_runs(from_owner);
        uint64_t t2 = xk_os_monotonic_us(); prep_copy_us += t2 - t1;
        if (full_frame) view_copy(pristine_slot(0), shadow_of(0), (size_t)slots_used * XK_PAGE);
        else for (unsigned s = 0; s < slots_used; ++s) if (sw_slot[s]) memcpy(pristine_slot(s), shadow_of(s), XK_PAGE);
        prep_pristine_us += xk_os_monotonic_us() - t2;
    }
    for (unsigned i = 0; i < img_listed_n; ++i) {
        uint32_t ip = img_list[i];
        memcpy(copy_of_img(ip), live_of_img(ip), XK_PAGE);
        if (full_frame || sw_img[ip]) memcpy(pristine_img(ip), copy_of_img(ip), XK_PAGE);
    }
    bytes_in += ((uint64_t)slots_used + img_listed_n) * XK_PAGE;
}
/* Owner, at the dispatch join, tick N+1 complete, scene N possibly still bound on bank cur_bank: copy the listed slots
 * into the other bank. Nothing here touches the slot listing, full_frame or the bound bank (the helper's merge reads
 * them concurrently). */
void xv_render_view_early_copy(void)
{
    if (!early_mode || !ready || early_valid || prepared || img_listed_n || !slots_used || slots_used > bank_pages) return;
    uint64_t t0 = xk_os_monotonic_us();
    unsigned b = cur_bank ^ 1u, n = slots_used;
    /* The scene already signalled done (steady cinematic: the helper idles ~7 ms before the owner arrives): its core
     * is free, so the split assistant on that core takes half the runs. A still-running scene keeps its core. */
    extern int xv_scene_thread_helper_done(void) __attribute__((weak));
    int idle = split_idle && xv_scene_thread_helper_done && xv_scene_thread_helper_done();
    copy_runs_to(b, (idle ? 1 : 0) | (split_idle ? 2 : 0));
    early_split += idle;
    early_bank = b; early_slots = n; early_valid = 1; early_copies++;
    early_us += xk_os_monotonic_us() - t0;
}
static int early_finish(void)   /* after the join: 1 = the early bank is now the snapshot */
{
    if (!early_valid) return 0;
    early_valid = 0;
    uint64_t t0 = xk_os_monotonic_us();
    if (all_mode >= 3) list_object_pool();
    uint64_t t1 = xk_os_monotonic_us(); prep_list_us += t1 - t0;
    if (slots_used > bank_pages || slots_overflow) { early_fallbacks++; memset(merged_slot, 0, L.shadow_pages); return 0; }
    full_frame = active_frames < 30 || (full_interval && active_frames % full_interval == 0);
    active_frames++;
    cur_bank = early_bank;
    for (unsigned s = 0; s < early_slots; ++s) if (merged_slot[s]) { memcpy(shadow_of(s), live_of_slot(s), XK_PAGE); merged_slot[s] = 0; early_recopied++; }
    for (unsigned s = early_slots; s < slots_used; ++s) memcpy(shadow_of(s), live_of_slot(s), XK_PAGE);   /* appended by the listing */
    uint64_t t2 = xk_os_monotonic_us(); prep_copy_us += t2 - t1;
    if (full_frame) view_copy(pristine_slot(0), shadow_of(0), (size_t)slots_used * XK_PAGE);
    else for (unsigned s = 0; s < slots_used; ++s) if (sw_slot[s]) memcpy(pristine_slot(s), shadow_of(s), XK_PAGE);
    prep_pristine_us += xk_os_monotonic_us() - t2;
    bytes_in += (uint64_t)slots_used * XK_PAGE;
    return 1;
}
void xv_render_view_prepare(void)
{
    if (!ready || depth || bound || prepared) return;
    if (!learn_done && !all_mode) return;
    uint64_t t0 = xk_os_monotonic_us();
    if (!early_finish()) { if (merged_slot) memset(merged_slot, 0, L.shadow_pages); copy_in(1); }
    prepare_us += xk_os_monotonic_us() - t0; prepared = 1;
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
    if (prepared) prepared = 0; else copy_in(0);   /* not prepared by the owner (non-overlapped dispatch, in-place mode): copy here */
    if (active_frames == 1) log_request_table("first viewed frame");
    retarget_in_place(); bound_since_us = t0; bound = 1;
    if (thread_mode) xk_os_bind_page_table(rt->entries);       /* this thread (the one running the body) sees the view */
    { extern void xv_write_watch_arm(void) __attribute__((weak)); if (xv_write_watch_arm) xv_write_watch_arm(); }   /* host diagnostic (write_watch.c) */
    enter_us += xk_os_monotonic_us() - t0; frames_entered++; full_frames += full_frame;
}
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
/* 64 bytes of copy vs pristine per step; nonzero = some word in the block differs (the scene wrote it). */
static inline int block64_differs(const uint32_t *c, const uint32_t *p)
{
    uint32x4_t d = veorq_u32(vld1q_u32(c), vld1q_u32(p));
    d = vorrq_u32(d, veorq_u32(vld1q_u32(c + 4), vld1q_u32(p + 4)));
    d = vorrq_u32(d, veorq_u32(vld1q_u32(c + 8), vld1q_u32(p + 8)));
    d = vorrq_u32(d, veorq_u32(vld1q_u32(c + 12), vld1q_u32(p + 12)));
    uint32x2_t r = vorr_u32(vget_low_u32(d), vget_high_u32(d));
    return (vget_lane_u32(r, 0) | vget_lane_u32(r, 1)) != 0;
}
#else
static inline int block64_differs(const uint32_t *c, const uint32_t *p)
{
    uint32_t d = 0; for (unsigned k = 0; k < 16; ++k) d |= c[k] ^ p[k]; return d != 0;
}
#endif
static int merge_page(const uint8_t *copy, const uint8_t *pre, uint8_t *live, uint32_t page_id)   /* one pass; newlib memcmp is ~86 MB/s here */
{
    const uint32_t *c = (const uint32_t *)copy, *p = (const uint32_t *)pre; uint32_t *l = (uint32_t *)live; unsigned changed = 0;
    for (unsigned b = 0; b < XK_PAGE / 4; b += 16) {
        /* The A9's own prefetch leaves every line of the cold pristine page a serial miss (Vita ~79 us/page vs the
         * Pi's 3 us): request both streams 256 bytes ahead so the misses overlap. */
        if (b + 64 < XK_PAGE / 4) { __builtin_prefetch(c + b + 64); __builtin_prefetch(p + b + 64); }
        if (!block64_differs(c + b, p + b)) continue;   /* perf162: ~79 us/page with 4-word scalar blocks */
    for (unsigned i = b; i < b + 16; i += 4) {
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
    }
    words_merged += changed;
    return changed != 0;
}
static void unbind_merge(void)
{
    uint64_t t0 = xk_os_monotonic_us();
    restore_in_place(); bound = 0;
    uint64_t t1 = xk_os_monotonic_us(); leave_restore_us += t1 - t0;
    for (unsigned s = 0; s < slots_used; ++s) {
        if (!full_frame && !sw_slot[s]) continue;
        merged_pages_compared++;
        if (!merge_page(shadow_of(s), pristine_slot(s), live_of_slot(s), slot_page[s])) continue;
        if (merged_slot) merged_slot[s] = 1;   /* early snapshot: re-copy this page from live after the join */
        if (!sw_slot[s]) { sw_slot[s] = 1; sw_slots++; if (active_frames > 30) sw_found_late++; }
    }
    for (unsigned i = 0; i < img_listed_n; ++i) {
        uint32_t ip = img_list[i];
        if (!full_frame && !sw_img[ip]) continue;
        if (!merge_page(copy_of_img(ip), pristine_img(ip), live_of_img(ip), (L.image_off >> 12) + ip)) continue;
        if (!sw_img[ip]) { sw_img[ip] = 1; sw_imgs++; if (active_frames > 30) sw_found_late++; }
    }
    leave_merge_us += xk_os_monotonic_us() - t1;
}
void xv_render_view_leave(unsigned *scope)
{
    uint64_t t0 = xk_os_monotonic_us();
    if (!ready || !depth) { last_leave_us = t0; return; }
    if (--depth || !*scope) { last_leave_us = t0; return; }
    if (bound) { uint64_t tb = xk_os_monotonic_us(); if (thread_mode) xk_os_bind_page_table(g_xpt); leave_bind_us += xk_os_monotonic_us() - tb; unbind_merge(); }
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
    XK_LOG("[render-view] %u frames: entered %u (full %u); slots %u (overflow %u) image pages %u listed, tail %u; copy-in %.2f ms/frame (%.0f KiB, owner-side %.2f ms: list %.2f copy %.2f pristine %.2f), merge %.2f ms/frame (%llu words); scene-write set %u slots + %u image pages (%u found late); retargets %u (+%u image, overflow %u), fiber switches in scene %u, remaps in scene %u; aliases max %u\n",
           frames, frames_entered, full_frames, slots_used, slots_overflow, img_listed_n, img_lo < L.image_pages ? L.image_pages - img_lo : 0,
           frames_entered ? (double)enter_us / frames_entered / 1000.0 : 0.0, frames_entered ? (double)bytes_in / frames_entered / 1024.0 : 0.0, frames_entered ? (double)prepare_us / frames_entered / 1000.0 : 0.0, frames_entered ? (double)prep_list_us / frames_entered / 1000.0 : 0.0, frames_entered ? (double)prep_copy_us / frames_entered / 1000.0 : 0.0, frames_entered ? (double)prep_pristine_us / frames_entered / 1000.0 : 0.0,
           frames_entered ? (double)leave_us / frames_entered / 1000.0 : 0.0, (unsigned long long)words_merged,
           sw_slots, sw_imgs, sw_found_late, retargets_last, image_entries, retargets_overflow, fiber_switches, mirrors_in_scene, aliases_max);
    if (early_copies || early_fallbacks) XK_LOG("[render-view] early snapshot %u per window (%.2f ms/frame hidden in the owner's wait, %u split with the idle helper core), %u fallbacks, %u pages re-copied after the join\n", early_copies, early_copies ? (double)early_us / early_copies / 1000.0 : 0.0, early_split, early_fallbacks, early_recopied);
    early_copies = early_fallbacks = early_recopied = early_split = 0; early_us = 0;
    if (copy_runs) XK_LOG("[render-view] copy runs %u per window (%.1f per frame; DMA %d, clib %d, split copies %u, three-way %u)\n", copy_runs, frames_entered ? (double)copy_runs / frames_entered : 0.0, dma_mode, clib_copy,
#ifdef __vita__
        assist_splits, assist_three);
    assist_splits = assist_three = 0;
#else
        0u, 0u);
#endif
    copy_runs = 0;
    if (frames_entered) XK_LOG("[render-view] leave split per frame: bind %.3f restore %.3f merge %.3f ms, %.1f pages compared (full frames included)\n",
        (double)leave_bind_us / frames_entered / 1000.0, (double)leave_restore_us / frames_entered / 1000.0, (double)leave_merge_us / frames_entered / 1000.0,
        (double)merged_pages_compared / frames_entered);
    leave_bind_us = leave_restore_us = leave_merge_us = 0; merged_pages_compared = 0;
    frames_entered = full_frames = 0; enter_us = leave_us = bytes_in = words_merged = 0; fiber_switches = mirrors_in_scene = 0; prepare_us = prep_list_us = prep_copy_us = prep_pristine_us = 0;   /* (was never reset: perf109 "owner-side 31 -> 68 ms" grew per window) */
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
