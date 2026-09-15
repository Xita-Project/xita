/*
 * xv_x86rt.c - out-of-line runtime for recompiled x86 code (see xv_x86rt.h).
 * Portable C: builds for the Vita (arm-vita-eabi-gcc) and for the host test harness.
 */
#include <stdio.h>
#ifndef __vita__
#include <execinfo.h>
#endif
#include <stdlib.h>
#include <time.h>
#include "xv_x86rt.h"
#include "kernel/xk_object_jobs.h"

#ifndef XV_RT_LOG
#  if defined(__vita__)
#    include <psp2/kernel/clib.h>
#    define XV_RT_LOG(...) sceClibPrintf("[xv/x86] " __VA_ARGS__)
#  else
#    define XV_RT_LOG(...) fprintf(stderr, "[xv/x86] " __VA_ARGS__)
#  endif
#endif

static unsigned g_unimpl_count;

void xv_unimpl(xctx *c, uint32_t eip, const char *what)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if(xv_is_object_job(c))xv_object_job_stop(c,eip,what);
#endif
    if (g_unimpl_count < 64)
        XV_RT_LOG("unimplemented %s at %08X (eax=%08X esp=%08X)\n", what, eip, c->r[0], c->r[4]);
    g_unimpl_count++;
}

/* Guest threads are cooperative fibers; real Xbox threads are preemptive.  Generated code calls this
 * on backward branches so busy-wait loops (Bink IO polling etc.) cannot starve other threads. */
void __attribute__((weak)) xk_yield(void);
void xv_preempt(xctx *c)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if(xv_is_object_job(c)) {
#if defined(__vita__)
        uintptr_t caller=(uintptr_t)__builtin_return_address(0);
        XV_RT_LOG("object job spin ARM caller %08X relative-to-xv_preempt %d\n",
                  (unsigned)caller,(int32_t)(caller-(uintptr_t)xv_preempt));
#endif
        XV_RT_LOG("object job spin regs %08X %08X %08X %08X %08X %08X %08X %08X\n",
                  c->r[0],c->r[1],c->r[2],c->r[3],c->r[4],c->r[5],c->r[6],c->r[7]);
        xv_object_job_stop(c,0,"job instruction budget exceeded");
    }
#endif
    static uint64_t n;
    /* Slice between cooperative yields.  Halo's vblank wait (0xBB060) is a pure busy loop when its
     * sleep flag is off, so the game thread only lets the vblank thread fire at these yields: 200k
     * back-edges was ~10 ms per fire on the Vita (28% of the frame spinning).  XV_PREEMPT_SLICE tunes it. */
    static int slice = -1; if (slice < 0) { const char *e = getenv("XV_PREEMPT_SLICE"); slice = (e && atoi(e) > 100) ? atoi(e) : 20000; }
    c->preempt = slice;
    { extern void xd3d_lockstep_preempt(xctx *) __attribute__((weak)); if (xd3d_lockstep_preempt) xd3d_lockstep_preempt(c); }
    if (++n <= 5 || (n & 0x3FF) == 0) {
        XV_RT_LOG("preempt #%llu esp=%08X\n", (unsigned long long)n, c->r[4]);
        if (getenv("XV_SPIN_BT")) {
            extern volatile uint32_t xv_cur_fn;
#if defined(__vita__)
            /* Resolve an untraced build's busy loop with its ELF: add this
             * relative offset to xv_preempt's symbol address. ASLR cancels. */
            uintptr_t caller = (uintptr_t)__builtin_return_address(0);
            XV_RT_LOG("spin ARM caller %08X relative-to-xv_preempt %d\n",
                      (unsigned)caller, (int32_t)(caller - (uintptr_t)xv_preempt));
#endif
            XV_RT_LOG("spin fn %08X regs %08X %08X %08X %08X %08X %08X %08X %08X\n",
                      xv_cur_fn, c->r[0], c->r[1], c->r[2], c->r[3],
                      c->r[4], c->r[5], c->r[6], c->r[7]);
            if (xv_cur_fn == 0x51E90u)
                XV_RT_LOG("spin polygon %08X count %08X vertices %08X\n",
                          c->r[3], X_M32(c->r[3] + 0x34), X_M32(c->r[3] + 0x38));
        }
#ifndef __vita__
        if ((n & 0x3FF) == 0 && getenv("XV_SPIN_BT")) {   /* host: who is spinning (guest stack + host backtrace + HLE histogram) */
            char sb[200]; int k = 0;
            for (unsigned i = 0; i < 24 && k < 180; ++i) { uint32_t w = X_M32(c->r[4] + 4 * i); if (w >= 0x11000 && w < 0x3A0000) k += snprintf(sb + k, sizeof sb - k, " %X", w); }
            XV_RT_LOG("  guest stack:%s\n", sb);
            void *bt[24]; int m = backtrace(bt, 24); backtrace_symbols_fd(bt, m, 2);
            extern void xd3d_hist_dump(const char *) __attribute__((weak)); extern void xd3d_hist_reset(void) __attribute__((weak));
            if (xd3d_hist_dump) { xd3d_hist_dump("[spin]"); xd3d_hist_reset(); }
            extern void xk_dump_threads(void) __attribute__((weak)); if (xk_dump_threads) xk_dump_threads();
        }
#endif
    }
    if (xk_yield) xk_yield();
}

void xv_trap(xctx *c, uint32_t eip)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if(xv_is_object_job(c))xv_object_job_stop(c,eip,"guest trap");
#endif
    XV_RT_LOG("TRAP at %08X: eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
              eip, c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6], c->r[7]);
    { char sb[512]; int n = 0;                                 /* guest stack words that look like code addresses */
      for (unsigned i = 0; i < 64 && n < 480; ++i) { uint32_t w = X_M32(c->r[4] + 4 * i); if (w >= 0x11000 && w < 0x3A0000) n += snprintf(sb + n, sizeof sb - n, " %X", w); }
      XV_RT_LOG("TRAP stack code ptrs:%s\n", sb); }
    for (;;) {
        if (xk_yield) xk_yield(); else abort();
    }
}

uint64_t x_rdtsc(void)
{
#if defined(__vita__)
    extern uint64_t sceKernelGetProcessTimeWide(void);
    return sceKernelGetProcessTimeWide() * 733u;        /* ~733 MHz Pentium III */
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec) * 733ull / 1000ull;   /* 733 MHz */
#endif
}

xv_fn_t xv_lookup(uint32_t eip)
{
    unsigned lo = 0, hi = xv_fn_table_count;
    while (lo < hi) {
        unsigned mid = (lo + hi) >> 1;
        if (xv_fn_table[mid].eip < eip) lo = mid + 1;
        else if (xv_fn_table[mid].eip > eip) hi = mid;
        else return xv_fn_table[mid].fn;
    }
    return NULL;
}

/* HLE functions reachable through function pointers (vtables, callbacks) */
extern const xv_fn_entry_t xv_hle_table[];
extern const unsigned xv_hle_table_count;
extern const xv_fn_entry_t xv_hle_extra[] __attribute__((weak));   /* xd3d.c: vtable-only HLE methods */

int xk_dispatch_magic(xctx *c, uint32_t target) __attribute__((weak));
extern const unsigned xv_guest_trace_enabled __attribute__((weak));
extern volatile uint32_t xv_cur_fn __attribute__((weak));

#ifdef XV_HLE_DISPATCH_CACHE
#include "xv_hle_dispatch_cache.h"
#endif

void xv_call(xctx *c, uint32_t target)
{
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
    if(xv_is_object_job(c)) {
        /* Immutable guest table only: do not race the ordinary dispatch cache
         * or enter an HLE callback through a vtable. */
        xv_fn_t job_fn=xv_lookup(target);
        if(!job_fn)xv_object_job_stop(c,target,"indirect non-guest target");
        job_fn(c);return;
    }
#endif
    { static uint64_t n; static int on = -1; if (on < 0) on = getenv("XV_CALL_SAMPLE") != NULL;
      if (on && (++n & 0xFFFFF) == 0) XV_RT_LOG("call sample #%lluM: target %08X\n", (unsigned long long)(n >> 20), target); }
    int traced = &xv_guest_trace_enabled && xv_guest_trace_enabled && &xv_cur_fn;
    uint32_t caller = traced ? xv_cur_fn : 0;
    if ((target & 0xFFFF0000u) == 0xFE000000u && xk_dispatch_magic) {
        if (traced) xv_cur_fn = target;
        int handled = xk_dispatch_magic(c, target);
        if (traced) xv_cur_fn = caller;
        if (handled) return;
    }
    /* Cache repeated guest targets before the binary search. Call volume varies
     * by scene; measure it instead of inferring a per-frame cost from code size. */
    static struct { uint32_t eip; xv_fn_t fn; } cache[4096];
    unsigned slot = (target >> 2) & 4095;
    xv_fn_t fn = cache[slot].eip == target ? cache[slot].fn : NULL;
#ifdef XV_HLE_DISPATCH_CACHE
    int is_hle;
    /* A cached HLE result has already passed the guest-first lookup. These
     * linked tables are immutable. Keep its classification separate from the
     * guest cache so tracing, callbacks and lookup priority remain unchanged. */
    if (!fn && (fn = hle_dispatch_find(target))) {
        is_hle = 1;
        goto dispatch_resolved;
    }
#endif
    if (!fn) fn = xv_lookup(target);
    if (fn) { cache[slot].eip = target; cache[slot].fn = fn; }
#ifdef XV_HLE_DISPATCH_CACHE
    is_hle = !fn;
#else
    int is_hle = !fn;
#endif
    if (!fn) {
        for (unsigned i = 0; i < xv_hle_table_count; ++i)
            if (xv_hle_table[i].eip == target && xv_hle_table[i].fn) { fn = xv_hle_table[i].fn; break; }
    }
    if (!fn && xv_hle_extra) {                      /* HLE symbols only ever reached through vtables */
        for (unsigned i = 0; xv_hle_extra[i].eip; ++i)
            if (xv_hle_extra[i].eip == target) { fn = xv_hle_extra[i].fn; break; }
    }
    if (!fn) {
        XV_RT_LOG("indirect call to unknown target %08X (eax=%08X [eax]=%08X ecx=%08X [esp]=%08X [esp+4]=%08X)\n", target,
                  c->r[0], X_M32(c->r[0]), c->r[1], X_M32(c->r[4]), X_M32(c->r[4] + 4));
        if (getenv("XV_LENIENT")) { c->r[0] = 0; c->r[4] += 4; return; }
        xv_trap(c, target);
        return;
    }
#ifdef XV_HLE_DISPATCH_CACHE
    if (is_hle) hle_dispatch_store(target, fn);
dispatch_resolved:
    if (is_hle) hle_dispatch_hle_calls++; else hle_dispatch_guest_calls++;
#endif
    if (traced && is_hle) xv_cur_fn = 0x80000000u | target;
    fn(c);
    /* Generated indirect calls and callback dispatch return here. Their callee
     * may have changed the sampler marker; subsequent caller work belongs to
     * the caller, just as XV_FN_BACK handles direct generated calls. */
    if (traced) xv_cur_fn = caller;
}

/* ---- 80-bit extended precision <-> double ---------------------------------------- */
/* Rare cross-page copies stay out of the recompiled game's hot instruction stream. */
void x_guest_read_pages(void *dst, uint32_t a, size_t size)
{
    uint8_t *p = dst;
    while (size) {
        size_t n = 4096u - (a & 0xFFFu);
        if (n > size) n = size;
        memcpy(p, X_G(a), n);
        p += n; a += (uint32_t)n; size -= n;
    }
}
void x_guest_write_pages(uint32_t a, const void *src, size_t size)
{
    const uint8_t *p = src;
    while (size) {
        size_t n = 4096u - (a & 0xFFFu);
        if (n > size) n = size;
        memcpy(X_G(a), p, n);
        p += n; a += (uint32_t)n; size -= n;
    }
}

double x87_load_f80(xctx *c, uint32_t a)
{
    (void)c;
    uint64_t mant; uint16_t se;
    x_guest_read(&mant, a, 8); x_guest_read(&se, a + 8, 2);
    int sign = se >> 15; int exp = se & 0x7FFF;
    if (exp == 0 && mant == 0) return sign ? -0.0 : 0.0;
    if (exp == 0x7FFF) return (mant << 1) ? NAN : (sign ? -INFINITY : INFINITY);
    double d = ldexp((double)mant, exp - 16383 - 63);
    return sign ? -d : d;
}

void x87_store_f80(xctx *c, uint32_t a, double v)
{
    (void)c;
    uint64_t mant = 0; uint16_t se = 0;
    int sign = signbit(v) ? 1 : 0;
    if (isnan(v)) { se = 0x7FFF; mant = 0xC000000000000000ull; }
    else if (isinf(v)) { se = 0x7FFF; mant = 0x8000000000000000ull; }
    else if (v == 0.0) { se = 0; mant = 0; }
    else {
        int e; double m = frexp(fabs(v), &e);           /* v = m * 2^e, m in [0.5,1) */
        mant = (uint64_t)ldexp(m, 64);
        se = (uint16_t)(e - 1 + 16383);
    }
    se |= (uint16_t)(sign << 15);
    x_guest_write(a, &mant, 8); x_guest_write(a + 8, &se, 2);
}

/* ---- string instructions ------------------------------------------------------------ */
#define STEP(sz) (c->df ? -(int32_t)(sz) : (int32_t)(sz))

static inline uint32_t ld(xctx *c, uint32_t a, unsigned sz)
{
    (void)c;
    if (sz == 1) return X_M8(a);
    if (sz == 2) { uint16_t v; x_guest_read(&v, a, 2); return v; }
    uint32_t v; x_guest_read(&v, a, 4); return v;
}
static inline void st(xctx *c, uint32_t a, unsigned sz, uint32_t v)
{
    (void)c;
    if (sz == 1) X_M8(a) = (uint8_t)v;
    else if (sz == 2) { uint16_t w = (uint16_t)v; x_guest_write(a, &w, 2); }
    else x_guest_write(a, &v, 4);
}

static void watch_range(xctx *c, const char *op, uint32_t dst, uint32_t len)
{
    static const char *e; static uint32_t wa; static int init;
    if (!init) { init = 1; e = getenv("XV_WATCH_ADDR"); if (e) wa = (uint32_t)strtoul(e, NULL, 16); }
    if (!wa || !(dst <= wa && wa < dst + len)) return;
    char sb[200]; int k = 0;
    for (unsigned i = 0; i < 24 && k < 180; ++i) { uint32_t w = X_M32(c->r[4] + 4 * i); if (w >= 0x11000 && w < 0x3A0000) k += snprintf(sb + k, sizeof sb - k, " %X", w); }
    XV_RT_LOG("WATCH %s hits %08X: dst %08X len %u esp %08X stack:%s\n", op, wa, dst, len, c->r[4], sb);
}
void x_str_movs(xctx *c, unsigned sz, int mode)
{
    if (mode != X_STR_ONCE) watch_range(c, "movs", c->df ? c->r[7] - c->r[1] * sz : c->r[7], c->r[1] * sz);
    if (mode == X_STR_ONCE) { st(c, c->r[7], sz, ld(c, c->r[6], sz)); c->r[6] += STEP(sz); c->r[7] += STEP(sz); return; }
    while (!c->df && c->r[1]) { /* Copy whole elements within both translated pages. */
        unsigned n = 4096u - (c->r[6] & 0xFFFu);
        unsigned dst_n = 4096u - (c->r[7] & 0xFFFu);
        if (dst_n < n) n = dst_n;
        unsigned count = n / sz;
        if (count > c->r[1]) count = c->r[1];
        n = count * sz;
        uint8_t *src = X_G(c->r[6]), *dst = X_G(c->r[7]);
        /* Forward overlapping REP MOVS propagates earlier writes; memmove would snapshot them. */
        if (!count || ((uintptr_t)dst > (uintptr_t)src && (uintptr_t)dst - (uintptr_t)src < n)) {
            st(c, c->r[7], sz, ld(c, c->r[6], sz)); count = 1; n = sz;
        } else memmove(dst, src, n);
        c->r[6] += n; c->r[7] += n; c->r[1] -= count;
    }
    while (c->r[1]) { st(c, c->r[7], sz, ld(c, c->r[6], sz)); c->r[6] += STEP(sz); c->r[7] += STEP(sz); c->r[1]--; }
}

void x_str_stos(xctx *c, unsigned sz, int mode)
{
    uint32_t v = c->r[0];
    if (mode != X_STR_ONCE) watch_range(c, "stos", c->df ? c->r[7] - c->r[1] * sz : c->r[7], c->r[1] * sz);
    if (mode == X_STR_ONCE) { st(c, c->r[7], sz, v); c->r[7] += STEP(sz); return; }
    uint32_t byte = v & 0xFFu;
    int repeated_byte = sz == 1 || (sz == 2 && (v & 0xFFFFu) == byte * 0x101u) ||
                        (sz == 4 && v == byte * 0x01010101u);
    while (!c->df && repeated_byte && c->r[1]) {
        /* Zero/byte-pattern word and dword fills need one translation per
         * page, not per element. Keep an element crossing a page boundary on
         * the existing split-write path; adjacent guest pages may not be
         * adjacent in the host arena. Backward/nonuniform fills stay scalar. */
        unsigned count = (4096u - (c->r[7] & 0xFFFu)) / sz;
        if (count > c->r[1]) count = c->r[1];
        if (!count) { st(c, c->r[7], sz, v); count = 1; }
        else memset(X_G(c->r[7]), (int)byte, count * sz);
        c->r[7] += count * sz; c->r[1] -= count;
    }
    while (c->r[1]) { st(c, c->r[7], sz, v); c->r[7] += STEP(sz); c->r[1]--; }
}

void x_str_lods(xctx *c, unsigned sz, int mode)
{
    do {
        if (mode != X_STR_ONCE && !c->r[1]) break;
        uint32_t v = ld(c, c->r[6], sz);
        if (sz == 1) X_R8L(0) = (uint8_t)v; else if (sz == 2) X_R16(0) = (uint16_t)v; else c->r[0] = v;
        c->r[6] += STEP(sz);
        if (mode != X_STR_ONCE) c->r[1]--;
    } while (mode != X_STR_ONCE);
}

static inline void cmp_flags(xctx *c, uint32_t a, uint32_t b, unsigned sz)
{
    uint32_t m = sz == 4 ? 0xFFFFFFFFu : ((1u << (sz * 8)) - 1u);
    X_FLAGS(XK_SUB, a, b, (a - b) & m, sz * 8);
}

void x_str_cmps(xctx *c, unsigned sz, int mode)
{
    for (;;) {
        if (mode != X_STR_ONCE && !c->r[1]) return;
        uint32_t a = ld(c, c->r[6], sz), b = ld(c, c->r[7], sz);
        cmp_flags(c, a, b, sz);
        c->r[6] += STEP(sz); c->r[7] += STEP(sz);
        if (mode == X_STR_ONCE) return;
        c->r[1]--;
        if (mode == X_STR_REPE && a != b) return;
        if (mode == X_STR_REPNE && a == b) return;
    }
}

void x_str_scas(xctx *c, unsigned sz, int mode)
{
    uint32_t v = sz == 1 ? (c->r[0] & 0xFF) : sz == 2 ? (c->r[0] & 0xFFFF) : c->r[0];
    for (;;) {
        if (mode != X_STR_ONCE && !c->r[1]) return;
        uint32_t b = ld(c, c->r[7], sz);
        cmp_flags(c, v, b, sz);
        c->r[7] += STEP(sz);
        if (mode == X_STR_ONCE) return;
        c->r[1]--;
        if (mode == X_STR_REPE && v != b) return;
        if (mode == X_STR_REPNE && v == b) return;
    }
}

#ifndef __vita__
/* Debugger convenience (gdb: print xv_guest_ptr(0x2F8CA0)): host address of a guest address (main.c has the Vita one). */
void *xv_guest_ptr(uint32_t a) { return X_G(a); }
#endif
