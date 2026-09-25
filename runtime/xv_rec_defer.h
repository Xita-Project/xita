/* xv_rec_defer.h - XV_REC_DEFER: the scene helper's draws recorded by a worker thread on another core.
 * Included once, at the end of runtime/xv_d3d.c: the worker runs the recorder's own static functions.
 *
 *   XV_REC_DEFER=0 (default) original: every draw is recorded inline by the thread that calls it
 *                1 verify: inline recording as in 0 (its list is rendered); every draw is also queued as below and,
 *                  at each drain point, replayed by the worker into a separate shadow list and shadow pools with the
 *                  whole recorder state saved around it; each shadow command is compared with the inline one
 *                  (every field, the index values, the constant window, the attribute snapshot, the texture words
 *                  and, after the capture drain, the vertex bytes each stream points at)
 *                2 deferred: the scene helper queues each draw and a worker thread records it
 *
 * The helper keeps everything that reads or writes the kernel's D3D state or answers the guest: the handle lookup,
 * xd3d_ps_sync, the texture-state table read, the index buffer header, the dirty constant range. It copies into the
 * queue the values the inline path would read at that instant: the render states SyncDrawState consumes, the pixel
 * shader constants and vertex attributes (when they changed since the last draw), the dirty constant rows, the fog
 * colour and alpha test, the index list (the guest may rewrite it right after the call, as D3D copies it into the push
 * buffer), and the page table the helper translates through (the render view's scene table). The worker applies them
 * to the recorder state and runs the unchanged record path.
 *
 * What the worker still reads from guest memory, later than the inline path would: vertex buffer data and headers,
 * texture headers, texture and palette bytes. The Xbox GPU reads those after the draw call too; verify mode reads them
 * at the drain point, later still, so any difference shows as a mismatch.
 *
 * Every other recorder entry (clear, render target, visibility tests, immediate-mode End, present, UI) drains the
 * worker first and then runs inline, so the recorder is only ever used by one thread at a time. The XV_OCCL object
 * tags are queued too (xk_occlusion.c calls them for every object); the helper answers their slot and the model matrix
 * from its own mirrors, and the worker checks both. Drain points: those entries, the render view's leave (before the
 * scene table is unbound), and the scene's end on the helper.
 *
 * No __thread state (the Vita's is emutls, shared between the helper and the owner): the queue has one producer (the
 * scene helper), one consumer (the worker), and a drain is only called while the helper does not produce. */
#include <psp2/kernel/threadmgr.h>

#ifdef XV_REC_DEFER_OFF
/* Unit tests that include xv_d3d.c without the kernel or a thread layer: the original entries only. */
void xv_rec_defer_inline_begin(void) {}
void xv_rec_defer_inline_end(void) {}
void xv_rec_defer_note_state(void) {}
void xv_rec_defer_drain(void) {}
void xv_rec_defer_report(unsigned frames) { (void)frames; }
int xv_rec_defer_draw(int indexed, uint32_t prim, uint32_t count, uint32_t data, uint32_t fnv)
{ (void)indexed; (void)prim; (void)count; (void)data; (void)fnv; return 0; }
void xv_rec_defer_verify_done(void) {}
unsigned xv_d3d_occl_begin(uint32_t handle, unsigned flags) { return occl_begin_impl(handle, flags); }
void xv_d3d_occl_end(void) { occl_end_impl(); }
int xv_d3d_occl_proxy(unsigned slot, float x0, float y0, float x1, float y1, float z) { return occl_proxy_impl(slot, x0, y0, x1, y1, z); }
const float *xv_d3d_occl_matrix(void) { return occl_matrix_impl(); }
#else
static xv_rec_opt g_opt_defer = XV_REC_OPT_INIT("XV_REC_DEFER");

enum { RD_DRAW = 1, RD_OCCL_BEGIN, RD_OCCL_END, RD_OCCL_PROXY, RD_MATRIX, RD_FRAME, RD_PASS, RD_VIS };
#include "xv_rec_queue.h"
typedef xv_rq_hdr rd_hdr;   /* type, mode (1 verify, 2 deferred), bytes: the whole record */
typedef struct rd_draw_s {
    rd_hdr h;
    uint32_t *pt;                    /* the page table the helper translated through */
    uint32_t vs_handle, prim, count, data, idata, index_base;
    uint32_t dirty_lo, dirty_hi;     /* the range SetTrackedConstants consumes (raw values) */
    uint32_t fog, atest;
    uint32_t ps_hash, ps_key;
    uint32_t stream_vb[4], stream_stride[4], texture[4], palette[4];
    uint32_t ts[4][5];
    xv_stencil stencil;
    uint32_t z_enable, z_write, z_func, cull, alpha_blend, blend_op, color_mask, src_blend, dst_blend;
    const void *index_identity;      /* xv_guest_ptr(idata) at call time: the index cache identity */
    uint32_t nidx;                   /* copied u16 indices (payload) */
    uint32_t real_first, real_n;     /* verify: the inline commands */
    uint32_t seq;
    uint32_t tag_gen;                /* index list in the map's tag data: read in place, this tag generation */
    uint8_t indexed, nodraw, has_psc, has_attr, rows_lo, rows_n, full_rows, index_tag;   /* rows: 0..191 */
    /* payload: psc[18][4] if has_psc, attributes[16][4] if has_attr, rows (full_rows ? 192 : rows_n) x 16 B, indices */
} rd_draw;
typedef struct { rd_hdr h; uint32_t handle, flags, slot; float m[16]; } rd_occl_begin;
typedef struct { rd_hdr h; uint32_t slot; float r[5]; } rd_occl_proxy;
typedef struct { rd_hdr h; float m[16]; } rd_matrix;
typedef struct { rd_hdr h; uint32_t value, frame; } rd_value;   /* RD_FRAME / RD_PASS / RD_VIS */

static xv_rq rd_q;                            /* producer: the scene helper; consumer: the worker */
static SceUID rd_thread = -1;
static int rd_started, rd_failed, rd_spin_us, rd_worker_timing;
static unsigned rd_draw_seq;
static rd_draw *rd_last_draw;                  /* verify: the queued record of the draw being recorded inline */
/* helper-side mirrors (producer thread only) */
extern unsigned xd3d_attr_gen, xd3d_psc_gen;   /* recomp/kernel/xd3d.c: bumped at every write */
static unsigned rd_h_psc_gen, rd_h_attr_gen;
static int rd_h_psc_valid, rd_h_attr_valid, rd_w_stale = 1, rd_timing = -1;
static uint32_t rd_h_frame = UINT32_MAX;
static unsigned rd_h_occl_n;
static float rd_h_mat[16];
static int rd_h_mode = -1;
/* worker-side mirrors (the recorder's owner) */
static float rd_w_vsc[192][4], rd_w_attr[16][4];
static xd3d_state_t rd_w_state;
static uint32_t *rd_w_pt;
/* verify: the shadow recorder's own S timeline (see rd_verify_batch_begin) */
static d3d_state_t rd_sh_S; static int rd_sh_S_valid; static unsigned rd_inline_gen;
/* statistics (per report window) */
static unsigned rd_stat_draws, rd_stat_events, rd_stat_inline_draws;
static uint64_t rd_stat_drain_us, rd_stat_worker_us, rd_stat_helper_us;
static unsigned rd_stat_slot_mismatch, rd_stat_matrix_mismatch;
static unsigned rd_stat_psc, rd_stat_attr, rd_stat_fullrows; static uint64_t rd_stat_rows, rd_stat_idx;
/* verify statistics */
static unsigned rd_v_draws, rd_v_streams;
static void rd_v_real_begin(void);
static void rd_v_real_end(struct rd_draw_s *p);

extern uint32_t xd3d_alpha_test(void);
extern unsigned xv_vertex_capture_tag_generation(void) __attribute__((weak));
/* The map's tag data (xv_vertex_capture.c TAG_PHYS_*): the game never rewrites it under a draw; file reads into it bump
 * the capture's tag generation, which a queued draw carries and the worker checks. */
enum { RD_TAG_BASE = 0x3A6000u, RD_TAG_BYTES = 0x1600000u };
static int rd_tag_resident(const void *p, uint32_t bytes)
{
    if (!xv_vertex_capture_tag_generation || !g_xram || (const uint8_t *)p < g_xram) return 0;
    uintptr_t off = (uintptr_t)((const uint8_t *)p - g_xram);
    return off >= RD_TAG_BASE && off + bytes <= RD_TAG_BASE + RD_TAG_BYTES;
}
static unsigned rd_stat_tag_idx, rd_stat_tag_gen_changed;
static inline int rd_mode(void) { return xv_rec_opt_mode(&g_opt_defer); }
extern int xv_scene_thread_on_helper(void) __attribute__((weak));
static inline int rd_on_helper(void) { return xv_scene_thread_on_helper && xv_scene_thread_on_helper(); }
extern void xk_os_bind_page_table(uint32_t *table) __attribute__((weak));

/* Bounded detail lines from the worker bypass the dropped-log policy (xv_log_criticalf on the Vita). */
extern void xv_log_criticalf(const char *fmt, ...) __attribute__((weak));
#define RD_CRIT(...) do { if (xv_log_criticalf) xv_log_criticalf("[xv/d3d] " __VA_ARGS__); else XV_LOG(__VA_ARGS__); } while (0)
static uint64_t rd_now(void) { return xk_os_monotonic_us ? xk_os_monotonic_us() : 0; }

/* ---- the queue (runtime/xv_rec_queue.h) and the worker ------------------------------------------------------ */
static void rd_process(rd_hdr *h);
static void rd_verify_batch_begin(void);
static void rd_verify_batch_end(void);
static inline void *rd_reserve(uint32_t bytes) { return xv_rq_reserve(&rd_q, bytes); }
static inline void rd_publish(int mode) { xv_rq_publish(&rd_q, mode == 2); }
static int rd_worker(SceSize args, void *argp)
{
    (void)args; (void)argp;
    { extern void xv_host_thread_role(int) __attribute__((weak)); if (xv_host_thread_role) xv_host_thread_role(1); }   /* host profiler */
    for (;;) {
        xv_rq_running(&rd_q);
        for (;;) {
            uint32_t head = __atomic_load_n(&rd_q.head, __ATOMIC_ACQUIRE), tail = rd_q.tail;
            rd_hdr *h = (rd_hdr *)xv_rq_at(&rd_q, &tail, head);
            if (!h) { if (tail != rd_q.tail) xv_rq_release(&rd_q, tail); break; }
            if (h->mode == 1 && !__atomic_load_n(&rd_q.want, __ATOMIC_ACQUIRE)) break;   /* verify: only at a drain */
            uint64_t t0 = rd_worker_timing ? rd_now() : 0;
            if (h->mode == 1) {   /* verify: the whole pending batch into the shadow list while the helper waits */
                rd_verify_batch_begin();
                do { rd_process(h); tail += h->bytes; } while ((h = (rd_hdr *)xv_rq_at(&rd_q, &tail, head)) != NULL);
                rd_verify_batch_end();
            } else {
                rd_process(h); tail += h->bytes;
            }
            /* A drained tail permits the owner to report/reset statistics. */
            if (rd_worker_timing) rd_stat_worker_us += rd_now() - t0;
            xv_rq_release(&rd_q, tail);
        }
        if (rd_spin_us && xv_rq_empty(&rd_q) && !__atomic_load_n(&rd_q.want, __ATOMIC_ACQUIRE)) {   /* XV_REC_DEFER_SPIN_US: poll before sleeping (not with verify records waiting) */
            uint64_t t0 = rd_now(); int work = 0;
            while (!work && rd_now() - t0 < (uint64_t)rd_spin_us)
                for (unsigned i = 0; i < 64 && !(work = !xv_rq_empty(&rd_q)); i++) {}
            if (work) continue;
        }
        xv_rq_idle(&rd_q, 4000);
    }
    return 0;
}
static int rd_start(void)
{
    if (rd_started) return 1;
    if (rd_failed) return 0;
    const char *e = getenv("XV_REC_DEFER_RING_KIB");
    uint32_t kib = e ? (uint32_t)atoi(e) : 1024u, bytes = 1u << 16;   /* a30 pod high water ~200-560 KiB; full = backpressure */
    while (bytes < kib * 1024u && bytes < (1u << 26)) bytes <<= 1;
    e = getenv("XV_REC_DEFER_BATCH"); int batch = e ? atoi(e) : 1; rd_q.batch = batch < 1 ? 1u : (unsigned)batch;
    e = getenv("XV_REC_DEFER_TIMING"); rd_timing = e ? atoi(e) : 0;   /* the helper's queueing time (two clock reads per draw) */
    e = getenv("XV_REC_WORKER_TIMING"); rd_worker_timing = e && atoi(e) != 0;
    e = getenv("XV_REC_DEFER_SPIN_US"); rd_spin_us = e ? atoi(e) : 0;   /* the worker polls this long before sleeping (fewer wake syscalls) */
    if (rd_spin_us < 0) rd_spin_us = 0;
    rd_q.ring = malloc(bytes); rd_q.size = bytes;
    rd_q.wake = sceKernelCreateEventFlag("xv_rec_wake", 0, 0, NULL);
    rd_q.done = sceKernelCreateEventFlag("xv_rec_done", 0, 0, NULL);
    e = getenv("XV_REC_DEFER_CORE"); int core = e ? atoi(e) : 0;
    int mask = core == 1 ? SCE_KERNEL_CPU_MASK_USER_1 : core == 2 ? SCE_KERNEL_CPU_MASK_USER_2 : SCE_KERNEL_CPU_MASK_USER_0;
    e = getenv("XV_REC_DEFER_PRIO"); int prio = sceKernelGetThreadCurrentPriority() + (e ? atoi(e) : 1);
    if (rd_q.ring && rd_q.wake >= 0 && rd_q.done >= 0)
        rd_thread = sceKernelCreateThread("xv_rec_worker", rd_worker, prio, 256 * 1024, 0, mask, NULL);
    if (!rd_q.ring || rd_thread < 0 || sceKernelStartThread(rd_thread, 0, NULL) < 0) {
        rd_failed = 1; XV_LOG("[rec-defer] worker unavailable: inline recording\n"); return 0;
    }
    rd_started = 1;
    XV_LOG("[rec-defer] worker on core %d (priority %d), %u KiB queue, wake every %u records, poll %d us before sleeping\n", core, prio, bytes >> 10, rd_q.batch, rd_spin_us);
    return 1;
}

/* The worker's log lines are dropped like the scene helper's (runtime/xv_log.c): it records what the helper did. */
int xv_rec_defer_on_worker(void) { return rd_thread >= 0 && sceKernelGetThreadId() == rd_thread; }
/* A drain: the worker has recorded (mode 2) or shadow-replayed and compared (mode 1) everything queued. Called from
 * any thread while the helper does not produce (the helper itself, or the owner after the scene's join). */
void xv_rec_defer_drain(void)
{
    if (!rd_started) return;
    rd_q.drains++;
    if (xv_rq_empty(&rd_q)) return;
    uint64_t t0 = rd_now();
    rd_q.drains_busy++;
    xv_rq_wait(&rd_q, 0, 64);
    rd_stat_drain_us += rd_now() - t0;
}

/* ---- helper side ---------------------------------------------------------------------------------------------- */
static void rd_frame_check(int mode)
{
    if (rd_h_frame == g_build_frame && rd_h_mode == mode) return;
    /* A new recording frame (the previous one was drained by its present) or a mode change: the recorder is idle. */
    rd_h_frame = g_build_frame; rd_h_mode = mode;
    rd_h_occl_n = 0;
    memcpy(rd_h_mat, &S.vsc[0][0], sizeof rd_h_mat);
    rd_w_stale = 1; rd_h_psc_valid = rd_h_attr_valid = 0;
    if (mode == 1) {   /* the shadow list starts from the real list's pass and query */
        rd_value *v = rd_reserve(sizeof *v); v->h.type = RD_FRAME; v->h.mode = 1;
        v->value = g_lists[g_build_frame % XV_NUM_LISTS]->cur_pass | (g_lists[g_build_frame % XV_NUM_LISTS]->active_visibility << 8);
        v->frame = g_build_frame;
        rd_publish(1);
    }
}
static int rd_mode_here(void)
{
    int mode = rd_mode();
    if (!mode || !rd_on_helper() || !rd_start()) return 0;
    rd_frame_check(mode);
    return mode;
}
/* Before any inline use of the recorder (every thread): drain, and bring the worker's constant mirror up to the
 * rows the inline code is about to consume. */
void xv_rec_defer_inline_begin(void)
{
    if (!rd_started) return;
    if (!rd_mode()) { rd_w_stale = 1; return; }
    xv_rec_defer_drain();
    rd_inline_gen = S.vsc_gen;
    uint32_t lo = xd3d_state.vsc_dirty_lo, hi = xd3d_state.vsc_dirty_hi;
    if (lo < hi && hi <= 192) memcpy(rd_w_vsc[lo], xd3d_state.vsc[lo], (size_t)(hi - lo) * 16u);
    else if (!(lo == 192 && hi == 0)) rd_w_stale = 1;
}
void xv_rec_defer_inline_end(void)
{
    int mode;
    if (!rd_started || !(mode = rd_mode())) return;
    memcpy(rd_h_mat, &S.vsc[0][0], sizeof rd_h_mat);   /* the recorder is idle: S is the inline timeline's */
    if (mode == 1 && rd_sh_S_valid) {
        /* Verify: in mode 2 the worker would continue from the state this inline code left. Take it, but keep the
         * shadow's own constant generation (bumped when the inline code bumped it), so the shadow list never matches a
         * snapshot generation from the inline timeline. */
        unsigned gen = rd_sh_S.vsc_gen + (S.vsc_gen != rd_inline_gen);
        memcpy(&rd_sh_S, &S, sizeof S); rd_sh_S.vsc_gen = gen;
    }
}
/* A scene-helper draw (xd3d_r_draw, after its guest-side checks). 1: queued for the worker (mode 2). 2: queued for the
 * verify shadow; the caller records inline, then calls xv_rec_defer_verify_done. 0: the caller records inline. */
int xv_rec_defer_draw(int indexed, uint32_t prim, uint32_t count, uint32_t data, uint32_t fnv)
{
    int mode = rd_mode_here();
    if (!mode) return 0;
    { extern int xd3d_hist_active(void); extern int xd3d_vertex_trace_active(void);
      if (xd3d_hist_active() || xd3d_vertex_trace_active()) { rd_stat_inline_draws++; return 0; } }   /* diagnostics: inline */
    uint64_t t0 = rd_timing ? rd_now() : 0;
    uint32_t h = xv_d3d_handle_for_hash(fnv);
    if (!h) return 0;                                   /* the inline path logs and skips it */
    uint32_t idata = 0, nidx = 0; const void *identity = NULL;
    if (indexed) {
        uint32_t ib = xd3d_state.indices;
        idata = data ? data : (ib ? *(const xu32_u *)X_G(ib + 4) : 0);
        if (idata) {
            if (count > (1u << 20)) return 0;           /* implausible: leave it to the inline path */
            identity = xv_guest_ptr(idata); nidx = count;
            if (rd_tag_resident(identity, count * 2u)) nidx = 0;   /* immutable map data: read in place */
        }
    }
    xd3d_ps_sync();
    int has_psc = !rd_h_psc_valid || rd_h_psc_gen != xd3d_psc_gen;
    const float (*attr)[4] = xd3d_current_attributes();
    int has_attr = !rd_h_attr_valid || rd_h_attr_gen != xd3d_attr_gen;
    uint32_t lo = xd3d_state.vsc_dirty_lo, hi = xd3d_state.vsc_dirty_hi;
    int valid = lo <= 192 && hi <= 192 && (lo <= hi || (lo == 192 && hi == 0));
    int full = rd_w_stale || !valid;
    uint32_t rows = full ? 192u : (lo < hi ? hi - lo : 0u);
    uint32_t bytes = sizeof(rd_draw) + (has_psc ? sizeof xd3d_state.psc : 0) + (has_attr ? sizeof rd_w_attr : 0) + rows * 16u + nidx * 2u;
    rd_draw *p = rd_reserve(bytes);
    p->h.type = RD_DRAW; p->h.mode = (uint16_t)mode;
    p->pt = X_PT;
    p->vs_handle = h; p->prim = prim; p->count = count; p->data = data; p->idata = idata; p->index_base = xd3d_state.index_base;
    p->dirty_lo = lo; p->dirty_hi = hi;
    p->fog = xd3d_state.fog_color; p->atest = xd3d_alpha_test();
    p->ps_hash = xd3d_state.ps_hash; p->ps_key = xd3d_state.ps_key;
    memcpy(p->stream_vb, xd3d_state.stream_vb, sizeof p->stream_vb); memcpy(p->stream_stride, xd3d_state.stream_stride, sizeof p->stream_stride);
    memcpy(p->texture, xd3d_state.texture, sizeof p->texture); memcpy(p->palette, xd3d_state.palette, sizeof p->palette);
    xd3d_texture_states(p->ts);
    p->stencil = xd3d_state.stencil;
    p->z_enable = xd3d_state.z_enable; p->z_write = xd3d_state.z_write; p->z_func = xd3d_state.z_func; p->cull = xd3d_state.cull;
    p->alpha_blend = xd3d_state.alpha_blend; p->blend_op = xd3d_state.blend_op; p->color_mask = xd3d_state.color_mask;
    p->src_blend = xd3d_state.src_blend; p->dst_blend = xd3d_state.dst_blend;
    p->index_identity = identity; p->nidx = nidx;
    p->index_tag = (uint8_t)(identity && !nidx); p->tag_gen = p->index_tag ? xv_vertex_capture_tag_generation() : 0;
    if (p->index_tag) rd_stat_tag_idx += count;
    p->real_first = p->real_n = 0; p->seq = rd_draw_seq++;
    p->indexed = (uint8_t)(indexed != 0); p->nodraw = (uint8_t)(indexed && !idata);
    p->has_psc = (uint8_t)has_psc; p->has_attr = (uint8_t)has_attr; p->full_rows = (uint8_t)full;
    p->rows_lo = (uint8_t)(full ? 0 : (lo < hi ? lo : 0)); p->rows_n = (uint8_t)(full ? 0 : rows);
    uint8_t *q = (uint8_t *)(p + 1);
    if (has_psc) { memcpy(q, xd3d_state.psc, sizeof xd3d_state.psc); rd_h_psc_gen = xd3d_psc_gen; rd_h_psc_valid = 1; q += sizeof xd3d_state.psc; }
    if (has_attr) { memcpy(q, attr, sizeof rd_w_attr); rd_h_attr_gen = xd3d_attr_gen; rd_h_attr_valid = 1; q += sizeof rd_w_attr; }
    if (full) { memcpy(q, xd3d_state.vsc, sizeof xd3d_state.vsc); q += sizeof xd3d_state.vsc; }
    else if (rows) { memcpy(q, xd3d_state.vsc[lo], rows * 16u); q += rows * 16u; }
    if (nidx) memcpy(q, identity, nidx * 2u);   /* the guest's list at call time */
    rd_w_stale = 0;
    rd_stat_psc += has_psc; rd_stat_attr += has_attr; rd_stat_fullrows += full; rd_stat_rows += rows; rd_stat_idx += nidx;
    /* After the worker's SetTrackedConstants the recorder's c[] is the kernel's (see rd_apply): the model matrix rows. */
    memcpy(rd_h_mat, xd3d_state.vsc[0], sizeof rd_h_mat);
    if (mode == 2) {
        xd3d_state.vsc_dirty_lo = 192; xd3d_state.vsc_dirty_hi = 0;   /* consumed, as the inline SetTrackedConstants does */
        rd_publish(2);
        rd_stat_draws++; if (rd_timing) rd_stat_helper_us += rd_now() - t0;
        return 1;
    }
    rd_last_draw = p;   /* verify: published after the inline record fills in its commands */
    rd_v_real_begin();
    return 2;
}
void xv_rec_defer_verify_done(void)
{
    rd_draw *p = rd_last_draw;
    if (!p) return;
    rd_last_draw = NULL;
    rd_v_real_end(p);
    rd_publish(1);
    rd_stat_draws++;
}

/* ---- XV_OCCL tags (recomp/kernel/xk_occlusion.c on the helper) --------------------------------------------- */
unsigned xv_d3d_occl_begin(uint32_t handle, unsigned flags)
{
    int mode = rd_mode_here();
    if (!mode) return occl_begin_impl(handle, flags);
    unsigned slot = occl_on() && rd_h_occl_n < XV_OCCL_SLOTS ? ++rd_h_occl_n : 0u;
    rd_occl_begin *e = rd_reserve(sizeof *e); e->h.type = RD_OCCL_BEGIN; e->h.mode = (uint16_t)mode;
    e->handle = handle; e->flags = flags; e->slot = slot; memcpy(e->m, rd_h_mat, sizeof e->m);
    if (mode == 2) { rd_publish(2); rd_stat_events++; return slot; }
    unsigned real = occl_begin_impl(handle, flags);
    if (xv_rec_opt_result(&g_opt_defer, real == slot && !memcmp(rd_h_mat, &S.vsc[0][0], sizeof rd_h_mat)))
        RD_CRIT("[rec-verify] XV_REC_DEFER mismatch frame %u: object slot %u/%u or model matrix\n", g_build_frame, real, slot);
    rd_publish(1); rd_stat_events++;
    return real;
}
void xv_d3d_occl_end(void)
{
    int mode = rd_mode_here();
    if (!mode) { occl_end_impl(); return; }
    rd_hdr *e = rd_reserve(sizeof *e); e->type = RD_OCCL_END; e->mode = (uint16_t)mode;
    if (mode == 1) occl_end_impl();
    rd_publish(mode); rd_stat_events++;
}
int xv_d3d_occl_proxy(unsigned slot, float x0, float y0, float x1, float y1, float z)
{
    int mode = rd_mode_here();
    if (!mode) return occl_proxy_impl(slot, x0, y0, x1, y1, z);
    rd_occl_proxy *e = rd_reserve(sizeof *e); e->h.type = RD_OCCL_PROXY; e->h.mode = (uint16_t)mode;
    e->slot = slot; e->r[0] = x0; e->r[1] = y0; e->r[2] = x1; e->r[3] = y1; e->r[4] = z;
    int r = mode == 1 ? occl_proxy_impl(slot, x0, y0, x1, y1, z) : 1;   /* the caller ignores the result */
    rd_publish(mode); rd_stat_events++;
    return r;
}
const float *xv_d3d_occl_matrix(void)   /* c[-96..-93]: world -> clip rows */
{
    int mode = rd_mode_here();
    if (mode != 2) return occl_matrix_impl();
    rd_matrix *e = rd_reserve(sizeof *e); e->h.type = RD_MATRIX; e->h.mode = 2;   /* the worker checks the mirror */
    memcpy(e->m, rd_h_mat, sizeof e->m);
    rd_publish(2); rd_stat_events++;
    return rd_h_mat;
}
/* Verify: the shadow list follows the inline render target and query changes (their entries run inline). */
static void rd_note_value(int type, uint32_t value)
{
    if (!rd_started || rd_mode() != 1 || !rd_on_helper()) return;
    rd_frame_check(1);
    rd_value *v = rd_reserve(sizeof *v); v->h.type = (uint16_t)type; v->h.mode = 1; v->value = value; v->frame = g_build_frame;
    rd_publish(1);
}
void xv_rec_defer_note_state(void)   /* after an inline entry that may change the pass or the open query */
{
    if (!rd_started || rd_mode() != 1) return;
    cmdlist_t *l = g_lists[g_build_frame % XV_NUM_LISTS];
    rd_note_value(RD_PASS, l->cur_pass);
    rd_note_value(RD_VIS, l->active_visibility);
}

/* ---- worker: apply a draw ----------------------------------------------------------------------------------- */
static void rd_bind(uint32_t *pt)
{
    if (pt == rd_w_pt) return;
    rd_w_pt = pt;
    if (xk_os_bind_page_table) xk_os_bind_page_table(pt);
}
static void rd_apply_draw(rd_draw *p)
{
    rd_bind(p->pt);
    const uint8_t *q = (const uint8_t *)(p + 1);
    if (p->has_psc) { memcpy(rd_w_state.psc, q, sizeof rd_w_state.psc); q += sizeof rd_w_state.psc; }
    if (p->has_attr) { memcpy(rd_w_attr, q, sizeof rd_w_attr); q += sizeof rd_w_attr; }
    if (p->full_rows) { memcpy(rd_w_vsc, q, sizeof rd_w_vsc); q += sizeof rd_w_vsc; }
    else if (p->rows_n) { memcpy(rd_w_vsc[p->rows_lo], q, (size_t)p->rows_n * 16u); q += (size_t)p->rows_n * 16u; }
    /* xd3d_r_draw's inline sequence, on the recorder state, from the call-time values */
    uint64_t profile = xv_draw_profile_begin();
    xv_d3d_SetVertexShader(p->vs_handle);
    uint32_t lo = p->dirty_lo, hi = p->dirty_hi;
    xv_d3d_SetTrackedConstants(rd_w_vsc, &lo, &hi);
    xv_draw_profile_step(XV_DRAW_SETUP, &profile);
    uint64_t state = xv_draw_profile_begin();
    rd_w_state.ps_hash = p->ps_hash; rd_w_state.ps_key = p->ps_key;
    memcpy(rd_w_state.stream_vb, p->stream_vb, sizeof p->stream_vb); memcpy(rd_w_state.stream_stride, p->stream_stride, sizeof p->stream_stride);
    memcpy(rd_w_state.texture, p->texture, sizeof p->texture); memcpy(rd_w_state.palette, p->palette, sizeof p->palette);
    rd_w_state.stencil = p->stencil;
    rd_w_state.z_enable = p->z_enable; rd_w_state.z_write = p->z_write; rd_w_state.z_func = p->z_func; rd_w_state.cull = p->cull;
    rd_w_state.alpha_blend = p->alpha_blend; rd_w_state.blend_op = p->blend_op; rd_w_state.color_mask = p->color_mask;
    rd_w_state.src_blend = p->src_blend; rd_w_state.dst_blend = p->dst_blend;
    xv_d3d_SyncDrawState(&rd_w_state, (const float (*)[4])rd_w_attr, (const uint32_t (*)[5])p->ts);
    xv_draw_profile_step(XV_DRAW_STATE, &state);
    if (p->nodraw) return;
    if (p->index_tag && p->tag_gen != xv_vertex_capture_tag_generation()) {   /* tag data read since the call */
        if (p->h.mode == 1) xv_rec_opt_result(&g_opt_defer, 0);
        if (rd_stat_tag_gen_changed++ < 4) RD_CRIT("[rec-defer] frame %u: a file read into the tag data between draw %u and its recording\n", g_build_frame, p->seq);
    }
    rec_override_t o = { p->fog, p->atest, p->nidx ? p->index_identity : NULL, p->nidx ? (const void *)q : NULL };
    g_rec_ovr = &o;
    if (p->indexed) { S.indices_dbg = p->idata; record_draw(p->prim, p->count, p->index_identity, p->index_base, NULL); }
    else xv_d3d_DrawVertices(p->prim, p->data, p->count);
    g_rec_ovr = NULL;
}
static void rd_verify_draw(rd_draw *p);
static void rd_verify_value(rd_value *v);
static void rd_process(rd_hdr *h)
{
    switch (h->type) {
    case RD_DRAW:
        if (h->mode == 1) rd_verify_draw((rd_draw *)h); else rd_apply_draw((rd_draw *)h);
        break;
    case RD_OCCL_BEGIN: {
        rd_occl_begin *e = (rd_occl_begin *)h;
        unsigned slot = occl_begin_impl(e->handle, e->flags);
        int ok = slot == e->slot && !memcmp(e->m, &S.vsc[0][0], sizeof e->m);
        if (h->mode == 1) { if (xv_rec_opt_result(&g_opt_defer, ok)) RD_CRIT("[rec-verify] XV_REC_DEFER mismatch frame %u: shadow object slot %u/%u or model matrix\n", g_build_frame, slot, e->slot); }
        else if (!ok && rd_stat_slot_mismatch++ < 4) RD_CRIT("[rec-defer] ERROR frame %u: object slot %u answered %u, or the model matrix differs\n", g_build_frame, slot, e->slot);
        break; }
    case RD_OCCL_END: occl_end_impl(); break;
    case RD_OCCL_PROXY: { rd_occl_proxy *e = (rd_occl_proxy *)h; occl_proxy_impl(e->slot, e->r[0], e->r[1], e->r[2], e->r[3], e->r[4]); break; }
    case RD_MATRIX: {
        rd_matrix *e = (rd_matrix *)h;
        if (memcmp(e->m, &S.vsc[0][0], sizeof e->m) && rd_stat_matrix_mismatch++ < 4)
            RD_CRIT("[rec-defer] ERROR frame %u: the helper's model matrix differs from the recorder's\n", g_build_frame);
        break; }
    case RD_FRAME: case RD_PASS: case RD_VIS: rd_verify_value((rd_value *)h); break;
    default: break;
    }
}

/* ---- verify: the shadow recorder ------------------------------------------------------------------------------ */
/* The worker replays a batch with the recorder's per-frame outputs swapped for shadow copies (the helper waits, the
 * pump only reads published lists): the command list, the quad, index and attribute pools and their fills, the index
 * cache, the object tag. S (and the draw counters the frame report reads) are saved and restored around the batch. */
typedef struct {
    cmdlist_t *list; uint16_t *quad; uint16_t *index; uint8_t *im; xv_index_cache *cache;
    uint32_t quad_used, index_used, im_used; uint64_t index_req, im_req; uint16_t occl_cur;
    uint32_t frame; int valid;
    uint32_t *stream_bytes;   /* per shadow command: captured bytes of each stream (verify) */
} rd_shadow_t;
static rd_shadow_t rd_sh;
static struct {
    cmdlist_t *list; uint16_t *quad; uint16_t *index; uint8_t *im; xv_index_cache *cache;
    uint32_t quad_used, index_used, im_used; uint64_t index_req, im_req; uint16_t occl_cur;
    unsigned draw_acc, bsp_acc;
} rd_saved;
static d3d_state_t rd_saved_S;
static uint32_t *rd_real_stream_bytes;       /* per real command, for the inline records of verify frames */
static struct { uint32_t real, shadow; } rd_pairs[XV_MAX_CMDS];
static unsigned rd_npairs;
static unsigned rd_v_real_before;
static void rd_v_real_begin(void)
{
    if (!rd_real_stream_bytes) rd_real_stream_bytes = calloc((size_t)XV_MAX_CMDS * XV_MAX_STREAMS, sizeof(uint32_t));
    rd_v_real_before = cur_list()->ncmds;
    g_rec_stream_bytes = rd_real_stream_bytes;
}
static void rd_v_real_end(struct rd_draw_s *p)
{
    g_rec_stream_bytes = NULL;
    p->real_first = rd_v_real_before; p->real_n = cur_list()->ncmds - rd_v_real_before;
}
static int rd_shadow_alloc(void)
{
    if (rd_sh.list) return 1;
    rd_sh.list = calloc(1, sizeof(cmdlist_t));
    rd_sh.quad = malloc(XV_QUAD_INDICES * sizeof(uint16_t));
    rd_sh.index = malloc(XV_FRAME_INDICES * sizeof(uint16_t));
    rd_sh.im = malloc(XV_IM_BYTES);
    rd_sh.cache = calloc(1, sizeof(xv_index_cache));
    rd_sh.stream_bytes = calloc((size_t)XV_MAX_CMDS * XV_MAX_STREAMS, sizeof(uint32_t));
    if (!rd_sh.list || !rd_sh.quad || !rd_sh.index || !rd_sh.im || !rd_sh.cache || !rd_sh.stream_bytes) {
        XV_LOG("[rec-defer] verify: no memory for the shadow recorder\n"); return 0;
    }
    xv_index_cache_reset(rd_sh.cache);
    return 1;
}
static int rd_shadow_on;
static void rd_verify_batch_begin(void)
{
    if (!rd_shadow_alloc()) return;
    unsigned L = g_build_frame % XV_NUM_LISTS;
    if (!rd_sh.valid || rd_sh.frame != g_build_frame) {   /* the shadow's BeginFrame */
        cmdlist_t *n = rd_sh.list;
        n->nui = 0; n->ncmds = 0; n->nconsts = 0; n->dropped = 0; n->const_dropped = 0;
        n->drop_commands = n->drop_indices = n->drop_attributes = n->drop_immediate = 0;
        n->nvisibility = n->active_visibility = 0; n->occl_n = 0; n->occl_armed = 0; n->visibility_gpu_ready = 0;
        n->cur_pass = g_lists[L]->cur_pass;
        rd_sh.quad_used = rd_sh.index_used = rd_sh.im_used = 0; rd_sh.index_req = rd_sh.im_req = 0; rd_sh.occl_cur = 0;
        if (index_metadata_enabled()) xv_index_cache_new_frame(rd_sh.cache); else xv_index_cache_reset(rd_sh.cache);
        rd_sh.frame = g_build_frame; rd_sh.valid = 1; rd_npairs = 0;
    }
    extern unsigned xv_d3d_draw_acc, xv_d3d_bsp_acc;
    rd_saved.list = g_lists[L]; rd_saved.quad = g_quad_indices; rd_saved.index = g_frame_indices; rd_saved.im = g_im_vertices;
    rd_saved.cache = g_index_cache; rd_saved.quad_used = g_quad_used[L]; rd_saved.index_used = g_index_used[L];
    rd_saved.im_used = g_im_used[L]; rd_saved.index_req = g_index_requested[L]; rd_saved.im_req = g_im_requested[L];
    rd_saved.occl_cur = g_occl_cur; rd_saved.draw_acc = xv_d3d_draw_acc; rd_saved.bsp_acc = xv_d3d_bsp_acc;
    memcpy(&rd_saved_S, &S, sizeof S);
    if (!rd_sh_S_valid) { memcpy(&rd_sh_S, &S, sizeof S); rd_sh_S_valid = 1; }
    memcpy(&S, &rd_sh_S, sizeof S);   /* the shadow's own timeline: S as its last batch or the last inline code left it */
    /* pool bases so that base + L * per-list lands on the shadow buffer (only that list's range is ever touched) */
    g_lists[L] = rd_sh.list;
    g_quad_indices = rd_sh.quad - (size_t)L * XV_QUAD_INDICES;
    g_frame_indices = rd_sh.index - (size_t)L * XV_FRAME_INDICES;
    g_im_vertices = rd_sh.im - (size_t)L * XV_IM_BYTES;
    g_index_cache = rd_sh.cache;
    g_quad_used[L] = rd_sh.quad_used; g_index_used[L] = rd_sh.index_used; g_im_used[L] = rd_sh.im_used;
    g_index_requested[L] = rd_sh.index_req; g_im_requested[L] = rd_sh.im_req; g_occl_cur = rd_sh.occl_cur;
    rd_shadow_on = 1;
}
static void rd_verify_streams(void);
static void rd_verify_batch_end(void)
{
    if (!rd_shadow_on) return;
    unsigned L = g_build_frame % XV_NUM_LISTS;
    extern unsigned xv_d3d_draw_acc, xv_d3d_bsp_acc;
    rd_sh.quad_used = g_quad_used[L]; rd_sh.index_used = g_index_used[L]; rd_sh.im_used = g_im_used[L];
    rd_sh.index_req = g_index_requested[L]; rd_sh.im_req = g_im_requested[L]; rd_sh.occl_cur = g_occl_cur;
    g_lists[L] = rd_saved.list; g_quad_indices = rd_saved.quad; g_frame_indices = rd_saved.index; g_im_vertices = rd_saved.im;
    g_index_cache = rd_saved.cache; g_quad_used[L] = rd_saved.quad_used; g_index_used[L] = rd_saved.index_used;
    g_im_used[L] = rd_saved.im_used; g_index_requested[L] = rd_saved.index_req; g_im_requested[L] = rd_saved.im_req;
    g_occl_cur = rd_saved.occl_cur; xv_d3d_draw_acc = rd_saved.draw_acc; xv_d3d_bsp_acc = rd_saved.bsp_acc;
    memcpy(&rd_sh_S, &S, sizeof S);
    memcpy(&S, &rd_saved_S, sizeof S);
    rd_shadow_on = 0;
    rd_verify_streams();
}
static void rd_verify_value(rd_value *v)
{
    if (!rd_shadow_on) return;
    if (v->h.type == RD_FRAME) { rd_sh.list->cur_pass = v->value & 0xFFu; rd_sh.list->active_visibility = v->value >> 8; }
    else if (v->h.type == RD_PASS) rd_sh.list->cur_pass = v->value;
    else rd_sh.list->active_visibility = v->value;
}
static int rd_mismatch(rd_draw *p, const char *what, unsigned a, unsigned b)
{
    if (xv_rec_opt_result(&g_opt_defer, 0))
        RD_CRIT("[rec-verify] XV_REC_DEFER mismatch frame %u draw %u (inline cmd %u): %s %u/%u\n", g_build_frame, p->seq, p->real_first, what, a, b);
    return 0;
}
static int rd_cmd_equal(rd_draw *p, const cmdlist_t *la, const cmd_t *a, const cmdlist_t *lb, const cmd_t *b)
{
#define RD_F(f) if (a->f != b->f) return rd_mismatch(p, #f, (unsigned)a->f, (unsigned)b->f)
    RD_F(kind); RD_F(fs_kind); RD_F(blend); RD_F(prim); RD_F(depth_func_idx); RD_F(depth_write); RD_F(cull); RD_F(ntex);
    RD_F(vs); RD_F(visibility); RD_F(index_count); RD_F(const_n); RD_F(fog_color); RD_F(atest);
    RD_F(loading_border_color); RD_F(loading_border_axes); RD_F(ps_entry); RD_F(pass); RD_F(previous_frame); RD_F(opaque_alpha);
#if XV_PACKED_VERTEX_LAYOUT
    RD_F(packed_vertex);
#endif
    RD_F(depth_prepared); RD_F(occl);
#undef RD_F
    if (memcmp(&a->stencil, &b->stencil, sizeof a->stencil)) return rd_mismatch(p, "stencil", 0, 0);
    if (memcmp(a->tex, b->tex, sizeof a->tex)) return rd_mismatch(p, "texture words", 0, 0);
    if (memcmp(a->texscale, b->texscale, sizeof a->texscale)) return rd_mismatch(p, "texture scale", 0, 0);
    if (memcmp(a->psc, b->psc, sizeof a->psc)) return rd_mismatch(p, "combiner constants", 0, 0);
    if (!a->indices != !b->indices) return rd_mismatch(p, "index pointer", 0, 0);
    if (a->indices && memcmp(a->indices, b->indices, (size_t)a->index_count * 2u)) return rd_mismatch(p, "index values", a->index_count, 0);
    if (a->const_n && memcmp(la->consts + (size_t)a->const_off * 4u, lb->consts + (size_t)b->const_off * 4u, (size_t)a->const_n * 16u))
        return rd_mismatch(p, "constant window", a->const_n, 0);
    if (!a->constant_stream != !b->constant_stream) return rd_mismatch(p, "attribute snapshot", 0, 0);
    if (a->constant_stream && memcmp(a->constant_stream, b->constant_stream, sizeof S.const_attr)) return rd_mismatch(p, "attribute values", 0, 0);
    return 1;
}
static void rd_verify_draw(rd_draw *p)
{
    if (!rd_shadow_on) return;
    unsigned before = rd_sh.list->ncmds;
    g_rec_stream_bytes = rd_sh.stream_bytes;
    rd_apply_draw(p);
    g_rec_stream_bytes = NULL;
    unsigned made = rd_sh.list->ncmds - before;
    rd_v_draws++;
    if (made != p->real_n) { rd_mismatch(p, "commands", p->real_n, made); return; }
    if (!made) { xv_rec_opt_result(&g_opt_defer, 1); return; }
    const cmdlist_t *real = rd_saved.list;
    if (rd_cmd_equal(p, real, &real->cmds[p->real_first], rd_sh.list, &rd_sh.list->cmds[before])) {
        xv_rec_opt_result(&g_opt_defer, 1);
        if (rd_npairs < XV_MAX_CMDS) { rd_pairs[rd_npairs].real = p->real_first; rd_pairs[rd_npairs].shadow = before; rd_npairs++; }
    }
}
/* After a batch: every capture submitted so far has landed; compare the vertex bytes each stream pointer holds. */
static void rd_verify_streams(void)
{
    if (!rd_npairs) return;
    xv_vertex_capture_drain();
    const cmdlist_t *real = g_lists[g_build_frame % XV_NUM_LISTS];
    unsigned slot = g_build_frame % XV_NUM_LISTS;
    for (unsigned i = 0; i < rd_npairs; i++) {
        const cmd_t *a = &real->cmds[rd_pairs[i].real], *b = &rd_sh.list->cmds[rd_pairs[i].shadow];
        int equal = a->kind == b->kind;
        for (unsigned s = 0; equal && s < XV_MAX_STREAMS; s++) {
            uint32_t na = rd_real_stream_bytes[rd_pairs[i].real * XV_MAX_STREAMS + s];
            uint32_t nb = rd_sh.stream_bytes[rd_pairs[i].shadow * XV_MAX_STREAMS + s];
            if (na != nb || !a->streams[s] != !b->streams[s]) { equal = 0; break; }
            if (!a->streams[s] || !na || a->streams[s] == b->streams[s]) continue;
            rd_v_streams++;
            if (memcmp(xv_vertex_upload_readback(slot, a->streams[s]), xv_vertex_upload_readback(slot, b->streams[s]), na)) equal = 0;
        }
        if (xv_rec_opt_result(&g_opt_defer, equal))
            RD_CRIT("[rec-verify] XV_REC_DEFER mismatch frame %u (inline cmd %u): vertex stream bytes\n", g_build_frame, rd_pairs[i].real);
    }
    rd_npairs = 0;
}

/* ---- report (every 60 frames, from the draw-profile report after the present's drain) ----------------------- */
void xv_rec_defer_report(unsigned frames)
{
    XV_REC_OPT_REPORT(&g_opt_defer, frames, XV_LOG);
    if (!rd_started || !frames) return;
    XV_LOG("[rec-defer] %u frames: %u draws %u events queued (%.1f KiB/frame, high %u KiB, %u full waits), %u inline draws; "
           "helper %.2f ms/frame queueing; worker %.2f ms/frame busy; drains %u (%u waited, %.2f ms/frame); verify draws %u streams %u; "
           "per frame: %.0f constant rows, %.1f full syncs, %.1f combiner and %.1f attribute sets, %.0f indices copied, %.0f read in place (tag data); "
           "errors (session): object slot/matrix %u, tag data rewritten %u; worker timing %s\n",
        frames, rd_stat_draws, rd_stat_events, (double)rd_q.bytes_published / 1024.0 / frames, rd_q.high >> 10, rd_q.full_waits,
        rd_stat_inline_draws, (double)rd_stat_helper_us / 1000.0 / frames, (double)rd_stat_worker_us / 1000.0 / frames,
        rd_q.drains, rd_q.drains_busy, (double)rd_stat_drain_us / 1000.0 / frames, rd_v_draws, rd_v_streams,
        (double)rd_stat_rows / frames, (double)rd_stat_fullrows / frames, (double)rd_stat_psc / frames, (double)rd_stat_attr / frames,
        (double)rd_stat_idx / frames, (double)rd_stat_tag_idx / frames, rd_stat_slot_mismatch + rd_stat_matrix_mismatch, rd_stat_tag_gen_changed,
        rd_worker_timing ? "enabled" : "disabled");
    rd_stat_tag_idx = 0;
    rd_stat_psc = rd_stat_attr = rd_stat_fullrows = 0; rd_stat_rows = rd_stat_idx = 0;
    rd_stat_draws = rd_stat_events = rd_stat_inline_draws = 0; rd_q.drains = rd_q.drains_busy = rd_q.full_waits = 0;
    rd_q.bytes_published = 0; rd_q.high = 0; rd_stat_drain_us = rd_stat_worker_us = rd_stat_helper_us = 0;
    rd_v_draws = rd_v_streams = 0;
}
#endif   /* XV_REC_DEFER_OFF */
