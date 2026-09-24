/* XV_CACHE_DEFER: the scene helper never changes the shared resource caches while the tick can.
 *
 * Vita freeze 2026-09-23 (perf174, core psp2core-1790221795): the helper spun for good in f_000A6620, the block
 * allocator of the resource cache at [2E2D2C], whose walk over the block list only ends at the end of the cache - a
 * list damaged into a cycle never ends. On the Xbox the tick and the scene were one thread and the game never locks
 * these caches; under the overlap both allocate at once (XV_CACHE_PROBE on the host: helper 32A70/32510/A6620/33A20
 * and owner 17A750 -> 32A70/A6620/33A20 in the same in-flight scenes).
 *
 * During an overlapped scene a helper call to
 *   f_00032A70  (esi = entry: allocate in [2E2D2C], register the request in [2E2D24], queue the read via 33A20) or
 *   f_00032510  (edi = entry, one stack argument: the same for the cache at [2E2D38] / requests [2E2D30])
 * returns the function's own "no free block" result - exactly its failure exit - and is queued. The owner replays the
 * queue right after it joins the scene and before the next dispatch (xv_scene_thread_run), when neither the tick nor
 * the scene runs, re-checking the entry the way every caller does first ([esi+2Ch] / [edi+24h] still -1). Every caller
 * re-reads the entry after the call and treats -1 as not resident, so the data arrives a frame later.
 * The same holds for frees: f_000A65A0 (unlink a block from a cache's list and delete its datum) called by the helper
 * from f_00114B50 (aged-out entries of the cache at [2E358C], which the tick allocates from in 114D30) is queued; the
 * replay frees the block only if its handle is still live (the tick's allocator may have evicted it meanwhile).
 * Env XV_CACHE_DEFER: 1 on (default), 0 off. Entry hooks from tools/patch_cache_defer_hooks.py. */
#include "xk.h"
#include "../xv_x86rt.h"
#include <stdio.h>
#include <stdlib.h>

extern int xv_scene_thread_on_helper(void);
extern int xv_scene_thread_overlapped(void);
extern void f_00032A70(xctx *restrict c);
extern void f_00032510(xctx *restrict c);
extern void f_000A65A0(xctx *restrict c);

#define DEFER_MAX 256
static struct { uint32_t fn, entry, arg; } q[DEFER_MAX];
static volatile unsigned qn;                       /* written by the helper while a scene is in flight, read by the owner after the join */
static unsigned n_deferred, n_dup, n_full, n_replayed, n_resident, n_dropped, n_esp, n_free_other, n_free_stale;
static int mode = -1;

static int defer_mode(void)
{
    if (mode < 0) { const char *e = getenv("XV_CACHE_DEFER"); mode = e ? atoi(e) : 1;
        XK_LOG("[cache-defer] %s\n", mode ? "on: helper cache allocations during overlapped scenes are replayed by the owner after the join" : "off"); }
    return mode;
}

/* Entry hook: nonzero = handled (the guest function returns at once with its failure result). */
int xv_cache_defer(xctx *c, uint32_t fn)
{
    if (!xv_scene_thread_on_helper() || !xv_scene_thread_overlapped() || !defer_mode()) return 0;
    if (fn == 0xA65A0u && X_M32(c->r[4]) != 0x114BE5u) { n_free_other++; return 0; }   /* only 114B50's free is known to reach here */
    uint32_t entry = fn == 0x32A70u ? c->r[6] : fn == 0xA65A0u ? c->r[3] : c->r[7];   /* A65A0: ebx = block handle, edi = cache */
    uint32_t arg = fn == 0x32510u ? X_M32(c->r[4] + 4u) : fn == 0xA65A0u ? c->r[7] : 0;
    unsigned n = qn, i;
    for (i = 0; i < n; ++i) if (q[i].fn == fn && q[i].entry == entry) break;
    if (i < n) n_dup++;
    else if (n < DEFER_MAX) { q[n].fn = fn; q[n].entry = entry; q[n].arg = arg; __atomic_store_n(&qn, n + 1, __ATOMIC_RELEASE); n_deferred++; }
    else n_full++;                                   /* the entry stays unallocated: the scene asks again next frame */
    if (fn == 0xA65A0u) {                            /* no result: 114B50 pops and returns right after (eax/ecx/edx are dead) */
        c->r[4] += 4u;
    } else if (fn == 0x32A70u) {                     /* 32A81..32AF0: A6620 returned -1; cmp ebp,-1; je; pop ebp; ret */
        c->r[0] = 0xFFFFFFFFu;
        X_FLAGS(XK_SUB, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 32);
        c->r[4] += 4u;
    } else {                                         /* 325B3..325B6: xor al,al; pop ebx; ret 4 (eax was A6620's -1) */
        c->r[0] = 0xFFFFFF00u;
        X_FLAGS(XK_LOGIC, 0xFFu, 0xFFu, 0u, 8);
        c->r[4] += 8u;
    }
    return 1;
}

/* Owner, after the join and before the next dispatch; `c` is the owner's guest context (restored afterwards).
 * live = 0 (gameplay no longer active: the entries may belong to an unloaded level) drops the queue. */
void xv_cache_defer_replay(xctx *c, int live)
{
    unsigned n = __atomic_load_n(&qn, __ATOMIC_ACQUIRE);
    if (!n) return;
    if (!live) { n_dropped += n; __atomic_store_n(&qn, 0u, __ATOMIC_RELEASE); return; }
    xctx save = *c;
    for (unsigned i = 0; i < n; ++i) {
        uint32_t fn = q[i].fn, entry = q[i].entry;
        uint32_t sp = save.r[4] - 256u;              /* below the owner's live frame */
        if (fn == 0x32A70u) {
            if (X_M32(entry + 0x2Cu) != 0xFFFFFFFFu) { n_resident++; continue; }
            *c = save; c->r[4] = sp; c->r[6] = entry;
            X_PUSH32(0u);                            /* return address: never executed, the C call returns */
            f_00032A70(c);
            if (c->r[4] != sp) n_esp++;
        } else if (fn == 0xA65A0u) {
            uint32_t cache = q[i].arg, blocks = X_M32(X_M32(cache + 0x3Cu) + 0x34u);
            if (X_M16(blocks + (entry & 0xFFFFu) * 0x1Cu) != (entry >> 16)) { n_free_stale++; continue; }   /* datum salt: evicted and reused/freed already */
            *c = save; c->r[4] = sp; c->r[7] = cache; c->r[3] = entry;
            X_PUSH32(0u);
            f_000A65A0(c);
            if (c->r[4] != sp) n_esp++;
        } else {
            if (X_M32(entry + 0x24u) != 0xFFFFFFFFu) { n_resident++; continue; }
            *c = save; c->r[4] = sp; c->r[7] = entry;
            X_PUSH32(q[i].arg); X_PUSH32(0u);
            f_00032510(c);                           /* ret 4 */
            if (c->r[4] != sp) n_esp++;
        }
        n_replayed++;
    }
    *c = save;
    __atomic_store_n(&qn, 0u, __ATOMIC_RELEASE);
}

void xv_cache_defer_report(unsigned frames)
{
    if (!(n_deferred | n_dup | n_full | n_replayed | n_resident | n_dropped | n_esp | n_free_other | n_free_stale)) return;
    XK_LOG("[cache-defer] %u frames: helper requests deferred %u (repeats %u, queue full %u); owner replayed %u, already resident %u, stale frees %u, dropped %u; helper frees not deferred %u%s\n",
           frames, n_deferred, n_dup, n_full, n_replayed, n_resident, n_free_stale, n_dropped, n_free_other, n_esp ? " STACK MISMATCH" : "");
    n_deferred = n_dup = n_full = n_replayed = n_resident = n_dropped = n_free_other = n_free_stale = 0;
}
