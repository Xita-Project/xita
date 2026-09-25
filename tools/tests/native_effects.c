/* Differential test of XV_NATIVE_EFFECTS (tools/test_native_effects.py builds it with the stage's lifted bodies and the
 * unit recomp/kernel/xk_native_effects.c).
 *
 * Synthetic guest arena: the XBE image window 0x180000..0x3A0000, a data window of shuffled physical pages at
 * 0x40000000 (records straddle page ends, split float accesses) and a stack of reversed pages at 0xD0000000; every other
 * page is the trash page. Memory is filled with a mix of pointers into the data window, small integers, floats and
 * special values, then shaped per hooked function (descriptor counts, shader-type words, effect records, qsort
 * arguments...). Guest callees outside the set are deterministic stand-ins that change registers, flags, the x87 stack
 * (sometimes to an unexpected depth, which sends an x87-regs body down its memory-lowering copy) and memory; the D3D
 * HLE entries are stand-ins with the real register/stack effects that log name, registers, arguments and the data they
 * read, and call xv_hle_tap like xd3d.c's XD3D_COUNT.
 *
 * Per case: mode 0 (the lifted body), mode 2 (the native) and mode 1 (verify: journaled native, undo, lifted body)
 * from identical state. The whole arena, every xctx field, the xv_preempt calls and the HLE log must match mode 0's
 * (NaN words against NaN words are counted); verify must log no MISMATCH. Output: "N cases, M mismatches, ...".
 * --bench N: game-like scenes, lifted vs native ns/call and (Linux perf) user instructions per call.
 * --threads N K: N threads call the hooks at once on private arenas through their own page tables. */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif
#include "xv_x86rt.h"
#include "xv_phase.h"

/* ---- runtime stand-ins ---------------------------------------------------------------------------------------- */
uint8_t *g_xram; uint32_t *g_xpt; uint8_t *g_img_base;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
__thread uint32_t *xv_host_page_table;
#define TABLE xv_host_page_table
#else
#define TABLE g_xpt
#endif
int xv_phase_enabled;
void xv_phase_begin(xv_phase_scope *s, void *c, unsigned id) { (void)s; (void)c; (void)id; }
void xv_phase_end(xv_phase_scope *s) { (void)s; }
void xv_scene_phase_begin(uint32_t a) { (void)a; }
void xv_scene_phase_end(uint32_t a) { (void)a; }
static __thread int log_mismatch; static int log_all;
void xk_os_log(const char *fmt, ...)
{
    if (strstr(fmt, "MISMATCH")) log_mismatch++;
    if (log_all || getenv("NX_DEBUG")) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
}
uint64_t xk_os_monotonic_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u; }
static __thread unsigned preempt_calls; static int slice = 5;
void xv_preempt(xctx *c) { preempt_calls++; c->preempt = slice; }
void xv_trap(xctx *c, uint32_t eip) { (void)c; fprintf(stderr, "trap %08X\n", eip); abort(); }
volatile uint32_t xv_cur_fn; int xv_hle_timing; unsigned xv_hle_timed_calls;
void xv_hle_time_add(const char *n, uint64_t us) { (void)n; (void)us; }
int xv_object_job_marker;
void xv_object_job_hle(xctx *c, uint32_t addr, void (*fn)(xctx *)) { (void)c; (void)addr; (void)fn; abort(); }
int xv_scene_thread_proxy_hle(xctx *c, void (*fn)(xctx *), const char *n) { (void)c; (void)fn; (void)n; return 0; }
void x_guest_read_pages(void *dst, uint32_t a, size_t size)
{
    uint8_t *d = dst;
    while (size) { size_t n = 4096u - (a & 0xFFFu); if (n > size) n = size; memcpy(d, g_xram + TABLE[a >> 12] + (a & 0xFFFu), n); d += n; a += (uint32_t)n; size -= n; }
}
void x_guest_write_pages(uint32_t a, const void *src, size_t size)
{
    const uint8_t *s = src;
    while (size) { size_t n = 4096u - (a & 0xFFFu); if (n > size) n = size; memcpy(g_xram + TABLE[a >> 12] + (a & 0xFFFu), s, n); s += n; a += (uint32_t)n; size -= n; }
}
/* the runtime's string stores (recomp/xv_x86rt.c semantics: element by element, page-split elements) */
void x_str_stos(xctx *c, unsigned sz, int mode)          /* recomp/xv_x86rt.c's (without its diagnostic watch) */
{
    uint32_t v = c->r[0];
    if (mode == X_STR_ONCE) { x_guest_write_pages(c->r[7], &v, sz); c->r[7] += c->df ? (uint32_t)-(int32_t)sz : sz; return; }
    uint32_t byte = v & 0xFFu;
    int repeated = sz == 1 || (sz == 2 && (v & 0xFFFFu) == byte * 0x101u) || (sz == 4 && v == byte * 0x01010101u);
    while (!c->df && repeated && c->r[1]) {
        unsigned count = (4096u - (c->r[7] & 0xFFFu)) / sz;
        if (count > c->r[1]) count = c->r[1];
        if (!count) { x_guest_write_pages(c->r[7], &v, sz); count = 1; }
        else memset(g_xram + TABLE[c->r[7] >> 12] + (c->r[7] & 0xFFFu), (int)byte, count * sz);
        c->r[7] += count * sz; c->r[1] -= count;
    }
    while (c->r[1]) { x_guest_write_pages(c->r[7], &v, sz); c->r[7] += c->df ? (uint32_t)-(int32_t)sz : sz; c->r[1]--; }
}
void x_str_movs(xctx *c, unsigned sz, int mode)
{
    do {
        if (mode != X_STR_ONCE && !c->r[1]) break;
        uint32_t v = 0; x_guest_read_pages(&v, c->r[6], sz); x_guest_write_pages(c->r[7], &v, sz);
        int32_t step = c->df ? -(int32_t)sz : (int32_t)sz; c->r[6] += (uint32_t)step; c->r[7] += (uint32_t)step;
        if (mode != X_STR_ONCE) c->r[1]--;
    } while (mode != X_STR_ONCE);
}
static uint32_t r32(uint32_t a) { uint32_t v; x_guest_read_pages(&v, a, 4); return v; }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void w16(uint32_t a, uint16_t v) { x_guest_write_pages(a, &v, 2); }
static void w8(uint32_t a, uint8_t v) { x_guest_write_pages(a, &v, 1); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }
static __thread uint32_t side_addr[4096]; static __thread unsigned side_n;   /* guest writes of the stand-ins (the undo test ignores them); per thread (host test only) */
static void side(uint32_t a, unsigned n) { for (unsigned i = 0; i < n && side_n < 4096; ++i) side_addr[side_n++] = a + i; }
static uint32_t fnv(uint32_t a, uint32_t n) { uint32_t h = 2166136261u; while (n--) { uint8_t b; x_guest_read_pages(&b, a++, 1); h = (h ^ b) * 16777619u; } return h; }
static uint32_t fnvf(uint32_t a, uint32_t n)        /* float data: NaN words made one (see the unit's nx_hash) */
{ uint32_t h = 2166136261u; for (uint32_t i = 0; i + 4 <= n; i += 4) { uint32_t w = r32(a + i); if ((w & 0x7F800000u) == 0x7F800000u && (w & 0x7FFFFFu)) w = 0x7FC00000u;
      for (unsigned k = 0; k < 4; ++k) h = (h ^ ((w >> (8 * k)) & 0xFFu)) * 16777619u; } return h; }

/* the constant-pack prefix (recomp/kernel/xk_constant_pack.c): declines on the helper; here it sometimes packs */
int xv_constant_pack_prefix(xctx *c)
{
    if (c->r[2] || !(c->r[6] & 0x10u)) return 0;
    uint32_t n = (uint32_t)(int16_t)(r32(c->r[6] + 4) & 0xFFFFu);
    if (n < 2 || n > 8) return 0;
    for (uint32_t i = 0; i + 1 < n; ++i) { w32(0x278258u + 48u * i + 12u, 0xABCD0000u + i); side(0x278258u + 48u * i + 12u, 4); }
    c->r[2] = n - 1; c->preempt -= (int32_t)(n - 1);
    return 1;
}

/* ---- D3D HLE stand-ins (real register/stack effects; logged) --------------------------------------------------- */
void (*volatile xv_hle_tap)(xctx *, const char *);
typedef struct { uint32_t id, r[8], args[4], data; } hrec;
typedef struct { unsigned n; hrec rec[512]; } hlog;
static __thread hlog *hl;
static int lean; static int tracing;                                                   /* --bench: stand-ins with only their register/stack effect */
static void hle_log(xctx *c, uint32_t id, const char *name, unsigned nargs, uint32_t data)
{
    if (xv_hle_tap) xv_hle_tap(c, name);
    if (!hl) return;
    if (hl->n >= 512) { hl->n++; return; }
    hrec *h = &hl->rec[hl->n++]; memset(h, 0, sizeof *h); h->id = id; memcpy(h->r, c->r, sizeof h->r);
    for (unsigned i = 0; i < nargs && i < 4; ++i) h->args[i] = r32(c->r[4] + 4 + 4 * i);
    h->data = data;
}
enum { D3D_G_RS = 0x2A0000u, D3D_G_TS = 0x2A2000u };
void xv_hle_D3DDevice_SetVertexShaderConstant(xctx *c)
{ if (lean) { c->r[0] = 0; c->r[4] += 16; return; } uint32_t n = r32(c->r[4] + 12); hle_log(c, 1, "D3DDevice_SetVertexShaderConstant", 3, hl && n <= 192 ? fnvf(r32(c->r[4] + 8), n * 16) : 0); c->r[0] = 0; c->r[4] += 16; }
void xv_hle_D3DDevice_SetPixelShaderProgram(xctx *c)
{ if (lean) { c->r[0] = 0; c->r[4] += 8; return; } uint32_t d = r32(c->r[4] + 4); hle_log(c, 2, "D3DDevice_SetPixelShaderProgram", 1, hl && d ? fnv(d, 0xF0) : 0); c->r[0] = 0; c->r[4] += 8; }
void xv_hle_D3DDevice_SetRenderStateNotInline(xctx *c)
{ if (lean) { c->r[0] = 0; c->r[4] += 12; return; } hle_log(c, 3, "D3DDevice_SetRenderStateNotInline", 2, 0); uint32_t st = r32(c->r[4] + 4), v = r32(c->r[4] + 8); if (st < 0x74) { w32(D3D_G_RS + st * 4, v); side(D3D_G_RS + st * 4, 4); } c->r[0] = 0; c->r[4] += 12; }
void xv_hle_D3DDevice_SetRenderState_PSTextureModes(xctx *c) { if (lean) { c->r[4] += 8; return; } hle_log(c, 4, "D3DDevice_SetRenderState_PSTextureModes", 1, 0); c->r[4] += 8; }
void xv_hle_D3DDevice_SetRenderState_Simple(xctx *c) { if (lean) { c->r[4] += 4; return; } hle_log(c, 5, "D3DDevice_SetRenderState_Simple", 0, 0); c->r[4] += 4; }
void xv_hle_D3DDevice_SetTexture(xctx *c) { if (lean) { c->r[0] = 0; c->r[4] += 12; return; } hle_log(c, 6, "D3DDevice_SetTexture", 2, 0); c->r[0] = 0; c->r[4] += 12; }

/* ---- guest callees outside the set: deterministic stand-ins ------------------------------------------------------ */
static uint32_t mix(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }
/* f_00173F20 (a periodic function of (word index, float t), `ret 8`, result in st(0)); sometimes leaves the x87 stack
 * one deeper or shallower than a normal return (the caller's x87-regs body then continues in its memory lowering) */
void f_00173F20(xctx *c)
{
    if (lean) { c->fsp = (c->fsp - 1u) & 7u; c->st[c->fsp] = 0.5; c->r[4] += 12u; return; }
    uint32_t E = c->r[4], a0 = r32(E + 4), a1 = r32(E + 8);
    if ((a1 & 0x7F800000u) == 0x7F800000u && (a1 & 0x7FFFFFu)) a1 = 0x7FC00000u;   /* NaN payloads are the compiler's operand order: the real callee's result does not depend on them either */
    uint32_t h = mix(a0 * 31u + a1 + c->fsw);
    if (tracing) printf("    173F20 stub: esp %08X a0 %08X a1 %08X fsw %04X fsp %u\n", E, a0, a1, c->fsw, c->fsp);
    float f; memcpy(&f, &a1, 4);
    double v = (h & 7u) == 0 ? (double)NAN : (double)f * 0.5 + (double)(a0 & 0xFFFFu) / 7.0;
    c->r[0] = h; c->r[1] ^= h >> 3; c->r[2] = a0 + 1u;
    X_FLAGS(XK_SUB, h, a0, h - a0, 32);
    w32(E - 8, h); w32(E - 12, a1); side(E - 12, 8);      /* its frame */
    unsigned depth = (h >> 8) % 23u == 0 ? 2u : (h >> 8) % 29u == 0 ? 0u : 1u;
    for (unsigned i = 0; i < depth; ++i) { c->fsp = (c->fsp - 1u) & 7u; c->st[c->fsp] = v + i; }
    c->fsw = (uint16_t)((c->fsw & ~0x4700u) | (h & 0x4500u) | ((c->fsp & 7u) << 11));
    c->r[4] = E + 12u;
}
/* f_000325C0 (texture cache: bitmap in eax, two word arguments, `ret 8`, eax = the texture or 0) */
void f_000325C0(xctx *c)
{
    if (lean) { c->r[0] = 0x40001000u; c->r[4] += 12u; return; }
    uint32_t E = c->r[4], bm = c->r[0], a0 = r32(E + 4), a1 = r32(E + 8), h = mix(bm ^ a0 * 7u ^ a1);
    w32(E - 4, c->r[3]); w32(E - 8, c->r[5]); w32(E - 12, c->r[6]); w32(E - 16, c->r[7]); side(E - 16, 16);   /* its pushes */
    if (bm >= 0x40000000u && bm < 0x40400000u) { w8(bm + 5u, 1); side(bm + 5u, 1); }            /* the usage byte */
    c->r[0] = (h & 3u) ? 0x40000000u + (h & 0x3FFFF0u) : 0; c->r[1] = h; c->r[2] = h >> 1;
    X_FLAGS(XK_LOGIC, 0, 0, c->r[0], 32);
    c->r[4] = E + 12u;
}

/* xv_call: the guest dispatch for the qsort comparators */
extern void f_0005DD50(xctx *);
enum { STUB_CMP = 0x00C0FFE0u };
static void stub_cmp(xctx *c)                                /* cdecl (a, b): the words, -1/0/1; a hash sometimes */
{
    uint32_t E = c->r[4], a = r32(E + 4), b = r32(E + 8), x = r32(a), y = r32(b);
    int r = x < y ? -1 : x > y; if ((mix(x ^ y) & 15u) == 0) r = (int)(mix(x + y) % 3u) - 1;
    c->r[0] = (uint32_t)r; c->r[1] = x; c->r[2] = y; X_FLAGS(XK_SUB, x, y, x - y, 32);
    c->r[4] = E + 4u;
}
void xv_call(xctx *c, uint32_t t)
{
    if (t == 0x5DD50u) { f_0005DD50(c); return; }
    if (t == STUB_CMP) { stub_cmp(c); return; }
    fprintf(stderr, "xv_call to %08X\n", t); abort();
}

/* ---- the hooks ----------------------------------------------------------------------------------------------- */
extern void xv_native_effects_force(int mode, int timing);
#ifndef NX_TEST_HOOKS
#define NX_TEST_HOOKS(X) X(0007E530)
#endif
#define HOOKS(X) NX_TEST_HOOKS(X)
#define DECL(a) extern void f_##a(xctx *);
HOOKS(DECL)
typedef struct { uint32_t addr; void (*fn)(xctx *); } hook;
#define ENT(a) { 0x##a##u, f_##a },
static const hook hooks[] = { HOOKS(ENT) };
#define NHOOKS (sizeof hooks / sizeof hooks[0])

/* ---- arena ----------------------------------------------------------------------------------------------------- */
enum { IMG_LO = 0x180000u, IMG_HI = 0x3A0000u, DATA = 0x40000000u, DATA_PAGES = 1024, STACK = 0xD0000000u, STACK_PAGES = 16 };
enum { IMG_PAGES = (IMG_HI - IMG_LO) >> 12, SPAN = (IMG_PAGES + DATA_PAGES + STACK_PAGES + 1) << 12 };
static unsigned arenas = 1;
static uint64_t rng_seed;
static __thread uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static uint32_t *make_table(unsigned k)                           /* arena copy k */
{
    uint32_t *t = malloc(sizeof(uint32_t) << 20); uint32_t base = k * SPAN, trash = base + SPAN - 4096;
    for (uint32_t i = 0; i < (1u << 20); ++i) t[i] = trash;
    for (uint32_t i = 0; i < IMG_PAGES; ++i) t[(IMG_LO >> 12) + i] = base + (i << 12);
    uint64_t save = rng; rng = 0x9E3779B97F4A7C15ull;               /* the same shuffle in every copy */
    uint32_t perm[DATA_PAGES]; for (uint32_t i = 0; i < DATA_PAGES; ++i) perm[i] = i;
    for (uint32_t i = DATA_PAGES - 1; i > 0; --i) { uint32_t j = rnd() % (i + 1), x = perm[i]; perm[i] = perm[j]; perm[j] = x; }
    rng = save;
    for (uint32_t i = 0; i < DATA_PAGES; ++i) t[(DATA >> 12) + i] = base + ((IMG_PAGES + perm[i]) << 12);
    for (uint32_t i = 0; i < STACK_PAGES; ++i) t[(STACK >> 12) + i] = base + ((IMG_PAGES + DATA_PAGES + STACK_PAGES - 1 - i) << 12);
    return t;
}
static float special(void)
{
    static const uint32_t bits[] = { 0x7FC00000u, 0xFFC00000u, 0x7F800000u, 0xFF800000u, 0, 0x80000000u, 0x00000001u, 0x007FFFFFu, 0x7F7FFFFFu, 0x3F800000u, 0xBF800000u, 0x7FA00001u };
    uint32_t b = bits[rnd() % (sizeof bits / sizeof bits[0])]; float f; memcpy(&f, &b, 4); return f;
}
static uint32_t word(void)
{
    uint32_t k = rnd() % 100;
    if (k < 30) return DATA + (rnd() % (DATA_PAGES << 12) & ~3u);
    if (k < 48) return rnd() % 8u;
    if (k < 52) return 0xFFFFFFFFu;
    if (k < 78) { float f = frand(-4, 4); uint32_t b; memcpy(&b, &f, 4); return b; }
    if (k < 86) { float f = special(); uint32_t b; memcpy(&b, &f, 4); return b; }
    return rnd();
}
static void fill(void)
{
    for (uint32_t a = IMG_LO; a < IMG_HI; a += 4) w32(a, word());
    for (uint32_t a = DATA; a < DATA + (DATA_PAGES << 12); a += 4) w32(a, word());
    for (uint32_t a = STACK; a < STACK + (STACK_PAGES << 12); a += 4) w32(a, word());
}
static uint32_t dptr(uint32_t bytes)                              /* a record in the data window (sometimes across a page end) */
{
    uint32_t a = DATA + (rnd() % ((DATA_PAGES << 12) - bytes - 64));
    if (rnd() % 7 == 0) a = (a | 0xFFFu) + 1u - (1u + rnd() % (bytes < 32 ? bytes : 32));
    else if (rnd() % 5) a &= ~3u;
    return a;
}
static void wrf(uint32_t a, int wild) { wf(a, wild && rnd() % 13 == 0 ? special() : frand(-3, 3)); }

/* ---- scenes ------------------------------------------------------------------------------------------------------ */
static void scene(xctx *c, uint32_t fn, int bench)
{
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = word();
    c->r[4] = STACK + 0x8000u + (rnd() % 0x4000u & ~3u);
    if (!bench && rnd() % 37 == 0) c->r[4] += 1 + rnd() % 3;        /* an unaligned stack now and then */
    const uint32_t E = c->r[4];
    w32(E, 0x00012345u);                                            /* the return address */
    c->fsp = rnd() & 7; c->fcw = (uint16_t)(0x027F | ((rnd() & 3u) << 10)); if (bench || rnd() % 4) c->fcw = 0x027F;
    c->fsw = (uint16_t)(rnd() & 0x7FFFu); for (unsigned i = 0; i < 8; ++i) c->st[i] = rnd() % 5 ? frand(-9, 9) : special();
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = (uint32_t[]){ 8, 16, 32 }[rnd() % 3];
    c->f_cf_override = rnd() & 1; c->f_cf = rnd() & 1; c->f_of_override = rnd() & 1; c->f_of = rnd() & 1; c->df = 0;
    c->preempt = bench ? 1 << 30 : (int32_t)(1 + rnd() % 40); c->fiber = 0;
    wf(0x1F0A68u, rnd() % 5 ? 0.0f : frand(-1, 1)); wf(0x1F0A78u, 1.0f); wf(0x1F0B40u, 0.017453292f); wf(0x1F0C88u, frand(0.5f, 2.0f));
    w32(0x1F0A70u, 0); w32(0x1F0A74u, 0x3FF00000u);
    switch (fn) {
    case 0x7E530: {
        uint32_t n = bench ? 3 + rnd() % 4 : rnd() % 9 == 0 ? rnd() % 70 : rnd() % 7; if (!bench && rnd() % 17 == 0) n = (uint32_t)-(int32_t)(rnd() % 3);
        uint32_t D = dptr(8), S = dptr(52u * (n & 0xFFu) + 4);
        if (!bench && rnd() % 13 == 0 && n > 1) D = 0x278258u + 48u * (rnd() % (n & 0xFFu)) + 4u * (rnd() % 12) - 4u;   /* the count is overwritten by a row */
        w32(D, S); w16(D + 4, (uint16_t)n); c->r[6] = D;
        for (uint32_t i = 0; i < 13u * (n & 0xFFu); ++i) wrf(S + 4 * i, !bench);
        if (D >= 0x278258u && D < 0x278258u + 48u * 64u) { for (uint32_t i = 0; i < (n & 0xFFu); ++i) w32(S + 52u * i + 0x28u, rnd() % 2 ? 0xFFFF8003u : rnd() % 70); }
        break; }
    case 0x7E420: {
        uint32_t S = dptr(0x50); w32(E + 4, S);
        wf(0x1E0B18u, bench || rnd() % 3 ? frand(0.5f, 2) : rnd() % 2 ? 0.0f : special());
        w16(S + 0x0C, (uint16_t)(rnd() % 4)); w16(S + 0x40, (uint16_t)(rnd() % 4));
        for (unsigned i = 0; i < 2; ++i) {
            uint32_t idx = rnd() % 4 ? rnd() % 6 : 0xFFFFFFFFu; w32(S + 0x44 + 4 * i, idx);
            if (idx != 0xFFFFFFFFu) { uint32_t L = 0x2FCF64u + idx * 0x38u, def = dptr(0x24); w32(L, def);
                for (unsigned k = 1; k < 14; ++k) wrf(L + 4 * k, !bench);
                if (rnd() % 4 == 0) w32(def + 0x1C, 0xBF800000u); else wrf(def + 0x1C, !bench); wrf(def + 0x20, !bench); }
        }
        for (unsigned k = 0; k < 20; ++k) wrf(S + 4 * k, !bench);
        break; }
    case 0x7E5D0: {
        uint32_t S = dptr(0x100); c->r[0] = S;
        uint32_t fl = rnd(); fl = (fl & ~0xFu) | (bench ? 1 + rnd() % 4 : rnd() % 8); w32(S + 0xD4, fl);
        break; }
    case 0x56F20: {
        uint32_t S = dptr(0x38); c->r[6] = S;
        for (unsigned k = 0; k < 14; ++k) wrf(S + 4 * k, !bench);
        for (unsigned k = 0; k < 3; ++k) { w16(S + 0x10 * k, (uint16_t)(rnd() % 4)); w16(S + 0x10 * k + 2, (uint16_t)(rnd() % 3));
            if (rnd() % 3 == 0) w32(S + 0x10 * k + 4, r32(0x1F0A68u)); }
        if (rnd() % 4) { uint32_t P = dptr(8), A = dptr(16); w32(P + 4, A); for (unsigned k = 0; k < 4; ++k) wrf(A + 4 * k, !bench); c->r[1] = P; } else c->r[1] = 0;
        for (unsigned k = 1; k <= 6; ++k) wrf(E + 4 * k, !bench);
        c->r[3] = dptr(16); c->r[7] = dptr(16);
        w8(0x27764Cu, (uint8_t)(rnd() & 1));
        break; }
    case 0x80360: case 0x80250: {
        uint32_t tags = dptr(0x400); w32(0x39CE24u, tags);
        for (unsigned i = 0; i < 16; ++i) { uint32_t g = rnd() % 5 ? dptr(0x70) : 0; w32(tags + 32 * i + 0x14, g);
            if (g) { uint32_t n = rnd() % 5; w32(g + 0x60, n); uint32_t arr = dptr(48 * 5); w32(g + 0x64, arr);
                     for (unsigned k = 0; k < 5; ++k) { w16(arr + 48 * k + 4, (uint16_t)rnd()); w16(arr + 48 * k + 6, (uint16_t)rnd()); w16(arr + 48 * k + 10, (uint16_t)(rnd() % 3)); } } }
        uint32_t sh = dptr(0x200); w32(0x2E3608u, sh);
        for (unsigned i = 0; i < 16; ++i) w32(sh + 16 * i + 0xB8, rnd() % 4 ? rnd() % 16 : 0xFFFFFFFFu);
        if (fn == 0x80360) {
            c->r[1] = rnd() % 4 ? rnd() % 16 : 0xFFFFFFFFu;
            w32(E + 4, rnd() % 4); w32(E + 8, rnd() % 16); w32(E + 12, rnd() % 5); w32(E + 16, rnd() % 7);
        } else { c->r[0] = rnd() % 4 ? dptr(0x30) : 0; w32(E + 4, rnd() % 4); }
        break; }
    case 0x11B60: case 0x11610: case 0x11BD0: {
        static const float ties[] = { 0.5f, 1.5f, 3.5f, -1.5f, 2.5f, -0.5f };   /* x * 255 exactly half-way: 127.5, 382.5, 892.5 ... */
        uint32_t P = dptr(16); for (unsigned k = 0; k < 4; ++k) wf(P + 4 * k, rnd() % 11 == 0 ? special() : rnd() % 9 == 0 ? ties[rnd() % 6] : rnd() % 7 == 0 ? frand(-2, 3) : frand(0, 1));
        if (fn == 0x11B60 || fn == 0x11BD0) w32(E + 4, P); else { wf(E + 4, rnd() % 11 == 0 ? special() : rnd() % 9 == 0 ? ties[rnd() % 6] : frand(0, 1)); w32(E + 8, P); }
        break; }
    case 0x5DFF0: case 0x191E0: {
        uint32_t n = bench ? 20 + rnd() % 60 : rnd() % 9 == 0 ? rnd() % 64 : rnd() % 12;
        uint32_t recs = dptr(0xA0u * 64), idx = dptr(2u * 64 + 8);
        w32(0x2E34B8u, recs); w32(0x2E34C8u, idx); w32(0x2E34C0u, fn == 0x5DFF0 && !bench && rnd() % 19 == 0 ? (uint32_t)-(int32_t)(rnd() % 3) : n);
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t R = recs + 0xA0u * i;
            w8(R, (uint8_t)(rnd() % 3 == 0 ? 0x80 : 0)); w32(R + 8, rnd() % 4);
            if (rnd() % 5) { uint32_t sh = dptr(0x30); w32(R + 0x0C, sh); w16(sh + 0x24, (uint16_t)(1 + rnd() % 8)); w8(sh + 0x29, (uint8_t)rnd()); } else w32(R + 0x0C, 0);
            wf(R + 0x70, rnd() % 6 == 0 ? (float)(rnd() % 3) : rnd() % 23 == 0 ? special() : frand(0, 50)); w8(R + 0x9D, (uint8_t)(rnd() % 4 == 0));
        }
        if (fn == 0x191E0) {
            uint32_t width = rnd() % 3 == 0 ? (uint32_t[]){ 1, 4, 6, 8 }[rnd() % 4] : 2, cmp = rnd() % 2 ? 0x5DD50u : STUB_CMP;
            if (cmp == 0x5DD50u) width = 2;
            for (uint32_t i = 0; i < n; ++i) w16(idx + 2 * i, (uint16_t)(rnd() % 64));
            if (width != 2) { idx = dptr(width * n + 8); for (uint32_t i = 0; i < width * n; ++i) w8(idx + i, (uint8_t)(rnd() % 7)); }
            w32(E + 4, idx); w32(E + 8, n); w32(E + 12, width); w32(E + 16, cmp);
        }
        break; }
    }
}

/* ---- compare ---------------------------------------------------------------------------------------------------- */
typedef struct { unsigned cases, mismatches, verify_mismatches, nan_words, preempt_diff, hle_calls, undo_bad; } stats;
static int nan32(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x007FFFFFu); }
static int nan64(double d) { return d != d; }
static uint32_t *rev_page;                                        /* arena page -> guest page (arena 0, the first mapping) */
static uint8_t gbyte(const uint8_t *arena, uint32_t ga) { return arena[g_xpt[ga >> 12] + (ga & 0xFFFu)]; }
static int nan_window(const uint8_t *a0, const uint8_t *a1, size_t off)
{
    if (!rev_page) { rev_page = calloc(SPAN >> 12, sizeof *rev_page); for (uint32_t p = (1u << 20); p-- > 1; ) if (g_xpt[p] < SPAN - 4096u) rev_page[g_xpt[p] >> 12] = p; }
    uint32_t gp = rev_page[off >> 12]; if (!gp) return 0;
    uint32_t ga = (gp << 12) | (uint32_t)(off & 0xFFFu);
    for (uint32_t k = ga - 3u; k != ga + 1u; ++k) {
        uint32_t x = 0, y = 0;
        for (unsigned b = 0; b < 4; ++b) { x |= (uint32_t)gbyte(a0, k + b) << (8 * b); y |= (uint32_t)gbyte(a1, k + b) << (8 * b); }
        if (nan32(x) && nan32(y)) return 1;
    }
    return 0;
}
static int cmp_state(const char *what, uint32_t fn, const uint8_t *a0, const uint8_t *a1, size_t len, const xctx *x0, const xctx *x1,
                     const hlog *h0, const hlog *h1, stats *st, int verbose)
{
    int bad = 0;
    for (size_t i = 0; i + 4 <= len; i += 4) {
        uint32_t u, v; memcpy(&u, a0 + i, 4); memcpy(&v, a1 + i, 4);
        if (u == v) continue;
        if (nan32(u) && nan32(v)) { st->nan_words++; continue; }
        /* a float stored at any guest address (split across pages too): every differing byte inside a 4-byte guest
         * window that is a NaN on both sides */
        { int ok = 1;
          for (size_t p = i; p < i + 4 && ok; ++p) if (a0[p] != a1[p]) ok = nan_window(a0, a1, p);
          if (ok) { st->nan_words++; continue; } }
        if (verbose && bad < 3) {
            uint32_t ga = 0; for (uint32_t p = 0; p < (1u << 20); ++p) if (g_xpt[p] == (i & ~0xFFFu) && p != 0) { ga = (p << 12) | (uint32_t)(i & 0xFFFu); break; }
            printf("  %s f_%08X: arena offset %zX (guest %08X, esp %08X): lifted %08X %s %08X\n", what, fn, i, ga, x0->r[4], u, what, v);
            if (getenv("NX_DUMP")) { printf("   lifted:"); for (int k = -8; k < 12; ++k) printf(" %02X", a0[i + k]); printf("\n   native:"); for (int k = -8; k < 12; ++k) printf(" %02X", a1[i + k]); printf("\n"); } }
        bad = 1; break;
    }
    const uint32_t *r0 = x0->r, *r1 = x1->r;
    for (unsigned i = 0; i < 8; ++i) if (r0[i] != r1[i]) {
        if (nan32(r0[i]) && nan32(r1[i])) { st->nan_words++; continue; }       /* a NaN float word loaded as an integer (aliased memory) */
        if (verbose) printf("  %s f_%08X: r%u lifted %08X vs %08X\n", what, fn, i, r0[i], r1[i]); bad = 1; }
#define F(f) if (x0->f != x1->f) { if (verbose) printf("  %s f_%08X: " #f " lifted %08X vs %08X\n", what, fn, (unsigned)x0->f, (unsigned)x1->f); bad = 1; }
    F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_cf) F(f_of_override) F(f_of) F(fsp) F(fsw) F(fcw) F(preempt)
#undef F
    for (unsigned i = 0; i < 8; ++i) { uint64_t p, q; memcpy(&p, &x0->st[i], 8); memcpy(&q, &x1->st[i], 8);
        if (p != q) { if (nan64(x0->st[i]) && nan64(x1->st[i])) st->nan_words++; else { if (verbose) printf("  %s f_%08X: st[%u] lifted %a vs %a\n", what, fn, i, x0->st[i], x1->st[i]); bad = 1; } } }
    if (h0 && h1) {
        if (h0->n != h1->n) { if (verbose) printf("  %s f_%08X: HLE calls %u vs %u\n", what, fn, h0->n, h1->n); bad = 1; }
        else for (unsigned i = 0; i < h0->n && i < 512; ++i) if (memcmp(&h0->rec[i], &h1->rec[i], sizeof h0->rec[i])) {
            hrec a = h0->rec[i], b = h1->rec[i];
            for (unsigned k = 0; k < 8; ++k) if (a.r[k] != b.r[k] && nan32(a.r[k]) && nan32(b.r[k])) { a.r[k] = b.r[k]; st->nan_words++; }
            if (!memcmp(&a, &b, sizeof a)) continue;
            if (verbose) printf("  %s f_%08X: HLE call %u (id %u) differs\n", what, fn, i, h0->rec[i].id); bad = 1; break; }
    }
    return bad;
}

static uint32_t pick(const char *funcs)
{
    if (!funcs || !*funcs) return hooks[rnd() % NHOOKS].addr;
    uint32_t list[16]; unsigned n = 0;
    for (const char *p = funcs; *p && n < 16; ) { char *e; unsigned long v = strtoul(p, &e, 16); if (e == p) { ++p; continue; } list[n++] = (uint32_t)v; p = e; }
    return n ? list[rnd() % n] : hooks[0].addr;
}
static void (*fn_of(uint32_t a))(xctx *) { for (unsigned i = 0; i < NHOOKS; ++i) if (hooks[i].addr == a) return hooks[i].fn; abort(); }

/* ---- bench ------------------------------------------------------------------------------------------------------- */
static long perf_open(void)
{
#if defined(__linux__)
    struct perf_event_attr at; memset(&at, 0, sizeof at); at.size = sizeof at; at.type = PERF_TYPE_HARDWARE;
    at.config = PERF_COUNT_HW_INSTRUCTIONS; at.exclude_kernel = 1; at.exclude_hv = 1;
    return syscall(SYS_perf_event_open, &at, 0, -1, -1, 0);
#else
    return -1;
#endif
}
static uint64_t perf_read(long fd) { uint64_t v = 0; if (fd >= 0 && read((int)fd, &v, 8) != 8) v = 0; return v; }
static double now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

static void bench(unsigned scenes, unsigned reps, const char *funcs)
{
    long pf = perf_open(); hl = 0; lean = 1;
    for (unsigned h = 0; h < NHOOKS; ++h) {
        uint32_t fn = hooks[h].addr;
        if (funcs && *funcs) { int want = 0; for (const char *p = funcs; *p; ) { char *e; unsigned long v = strtoul(p, &e, 16); if (e == p) { ++p; continue; } if (v == fn) want = 1; p = e; } if (!want) continue; }
        double t[2] = { 0, 0 }; uint64_t ins[2] = { 0, 0 }; unsigned calls = 0;
        uint8_t *save = malloc(SPAN);
        for (unsigned s = 0; s < scenes; ++s) {
            xctx c0; scene(&c0, fn, 1); memcpy(save, g_xram, SPAN);
            for (int mode = 0; mode < 2; ++mode) {
                xv_native_effects_force(mode ? 2 : 0, 0);
                memcpy(g_xram, save, SPAN);                             /* the scene; repetitions run on their own outputs */
                xctx cs[64]; unsigned nr = reps < 64 ? reps : 64;
                for (unsigned r = 0; r < nr; ++r) cs[r] = c0;
                void (*f)(xctx *) = fn_of(fn);
                f(&cs[0]); if (getenv("NX_BENCH_DEBUG")) printf("  f_%08X mode %d: edx %08X eax %08X esp %08X->%08X preempt %d\n", fn, mode, cs[0].r[2], cs[0].r[0], c0.r[4], cs[0].r[4], cs[0].preempt); cs[0] = c0;   /* warm */
                uint64_t i0 = perf_read(pf); double a = now_ns();
                for (unsigned r = 0; r < nr; ++r) f(&cs[r]);
                double b = now_ns(); uint64_t i1 = perf_read(pf);
                t[mode] += b - a; ins[mode] += i1 - i0; if (!mode) calls += nr;
            }
        }
        free(save);
        printf("bench f_%08X: %u calls: lifted %.1f ns %.0f insns, native %.1f ns %.0f insns: %.2fx (%.2fx instructions)\n", fn, calls,
               t[0] / calls, (double)ins[0] / calls, t[1] / calls, (double)ins[1] / calls, t[0] / t[1], ins[1] ? (double)ins[0] / ins[1] : 0.0);
    }
}

int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 1000; rng_seed = argc > 2 ? strtoull(argv[2], 0, 10) : 1;
    rng = 88172645463325252ull ^ (rng_seed * 0x9E3779B97F4A7C15ull); if (!rng) rng = 1;
    const char *funcs = 0; int do_bench = 0, reps = 0, nthreads = 0, iters = 0;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--funcs") && i + 1 < argc) funcs = argv[++i];
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) { do_bench = 1; reps = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--threads") && i + 2 < argc) { nthreads = atoi(argv[++i]); iters = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-v")) log_all = 1;
    }
    arenas = nthreads ? (unsigned)nthreads + 1 : 1;
    g_xram = calloc(arenas, SPAN); g_xpt = make_table(0); g_img_base = g_xram - IMG_LO;   /* image window at arena 0 */
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
    fill();
    if (do_bench) { bench(cases, (unsigned)reps, funcs); return 0; }
    if (nthreads) { extern int threads_test(unsigned, unsigned, unsigned, const char *); return threads_test(cases, (unsigned)nthreads, (unsigned)iters, funcs); }
    stats st; memset(&st, 0, sizeof st);
    uint8_t *save = malloc(SPAN), *ref = malloc(SPAN);
    hlog *h0 = calloc(1, sizeof *h0), *h1 = calloc(1, sizeof *h1);
    unsigned per_fn[64] = {0}, bad_fn[64] = {0};
    for (unsigned n = 0; n < cases; ++n) {
        if (n % 64 == 63) fill();
        xctx c0; uint32_t fn = pick(funcs); scene(&c0, fn, 0);
        tracing = getenv("NX_TRACE") && (unsigned)atoi(getenv("NX_TRACE")) == n;
        if (tracing) printf("case %u: esp %08X esi %08X ecx %08X fsp %u fsw %04X fcw %04X\n", n, c0.r[4], c0.r[6], c0.r[1], c0.fsp, c0.fsw, c0.fcw);
        unsigned hi = 0; while (hi < NHOOKS && hooks[hi].addr != fn) ++hi; per_fn[hi]++;
        memcpy(save, g_xram, SPAN);
        /* mode 0: the lifted body */
        xv_native_effects_force(0, 0); xctx cr = c0; h0->n = 0; hl = h0; preempt_calls = 0;
        if (tracing) printf("  lifted:\n");
        hooks[hi].fn(&cr); unsigned pr = preempt_calls; hl = 0;
        memcpy(ref, g_xram, SPAN);
        int bad = 0;
        /* mode 2: the native */
        memcpy(g_xram, save, SPAN); xv_native_effects_force(2, 0); xctx cn = c0; h1->n = 0; hl = h1; preempt_calls = 0;
        if (tracing) printf("  native:\n");
        hooks[hi].fn(&cn); hl = 0;
        if (preempt_calls != pr) { st.preempt_diff++; bad = 1; if (st.mismatches < 10) printf("  native f_%08X: xv_preempt calls %u vs lifted %u\n", fn, preempt_calls, pr); }
        bad |= cmp_state("native", fn, ref, g_xram, SPAN, &cr, &cn, h0, h1, &st, st.mismatches < 10);
        st.hle_calls += h0->n;
        /* mode 1: verify (both runs with an unbounded budget, then the lifted body's consumption: the budget matches
         * the lifted run's, the xv_preempt calls do not) */
        memcpy(g_xram, save, SPAN); xv_native_effects_force(1, 0); xctx cv = c0; log_mismatch = 0; h1->n = 0; hl = 0;
        hooks[hi].fn(&cv);
        cv.preempt = cr.preempt;                                    /* budget accounting differs by design; compared below */
        int vbad = cmp_state("verify", fn, ref, g_xram, SPAN, &cr, &cv, 0, 0, &st, st.mismatches < 10) | (log_mismatch != 0);
        if (log_mismatch && st.mismatches < 10) printf("  verify f_%08X: %d MISMATCH lines\n", fn, log_mismatch);
        if (vbad) st.verify_mismatches++;
        /* the journal: run the journaled native and undo it; the arena must equal the state before (the stand-ins' writes aside) */
        { extern int xv_native_effects_undo_test(uint32_t, xctx *);
          memcpy(g_xram, save, SPAN); xctx cu = c0; side_n = 0; hl = 0;
          int ok = xv_native_effects_undo_test(fn, &cu);
          if (ok > 0) {
              for (unsigned k = 0; k < side_n; ++k) { uint32_t a = side_addr[k]; g_xram[g_xpt[a >> 12] + (a & 0xFFFu)] = save[g_xpt[a >> 12] + (a & 0xFFFu)]; }
              if (memcmp(g_xram, save, SPAN)) {
                  size_t off = 0; while (off < SPAN && g_xram[off] == save[off]) ++off;
                  if (st.mismatches < 10) printf("  undo f_%08X: arena offset %zX not restored (before %02X after %02X)\n", fn, off, save[off], g_xram[off]);
                  vbad = 1; st.undo_bad++; } } }
        if (bad || vbad) { st.mismatches++; bad_fn[hi]++; if (st.mismatches <= 3) printf("  (case %u)\n", n); }
        st.cases++;
        memcpy(g_xram, save, SPAN);
    }
    printf("%u cases, %u mismatches (native vs lifted or verify), %u verify, %u undo, %u preempt-count differences, %u HLE calls, %u NaN words\n",
           st.cases, st.mismatches, st.verify_mismatches, st.undo_bad, st.preempt_diff, st.hle_calls, st.nan_words);
    for (unsigned i = 0; i < NHOOKS; ++i) if (per_fn[i]) printf("  f_%08X: %u cases, %u bad\n", hooks[i].addr, per_fn[i], bad_fn[i]);
    return st.mismatches ? 1 : 0;
}

/* N threads call the hooks at once, each on its own arena copy through its own page table; every result must equal the
 * single-threaded lifted body's on the same scene. Mode (native or verify) is set before the threads start. */
typedef struct { unsigned k, scenes, iters, bad, calls; const char *funcs; } tj;
static uint8_t **tref; static xctx *tctx_in, *tctx_ref; static uint32_t *tfn; static uint8_t *tsave;
static void *tmain(void *p)
{
    tj *j = p;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = make_table(j->k);
#endif
    uint8_t *mine = g_xram + (size_t)j->k * SPAN;
    for (unsigned it = 0; it < j->iters; ++it)
        for (unsigned s = 0; s < j->scenes; ++s) {
            memcpy(mine, tsave + (size_t)s * SPAN, SPAN);
            xctx c = tctx_in[s]; log_mismatch = 0;
            fn_of(tfn[s])(&c);
            xctx want = tctx_ref[s]; c.preempt = want.preempt;      /* verify applies the budget differently (see main) */
            stats st; memset(&st, 0, sizeof st);
            int bad = cmp_state("thread", tfn[s], tref[s], mine, SPAN, &want, &c, 0, 0, &st, j->bad < 3) | (log_mismatch != 0);
            j->bad += bad; j->calls++;
        }
    return 0;
}
int threads_test(unsigned scenes, unsigned n, unsigned iters, const char *funcs)
{
    if (scenes > 64) scenes = 64;
    tref = calloc(scenes, sizeof *tref); tctx_in = calloc(scenes, sizeof *tctx_in); tctx_ref = calloc(scenes, sizeof *tctx_ref);
    tfn = calloc(scenes, sizeof *tfn); tsave = malloc((size_t)scenes * SPAN);
    for (unsigned s = 0; s < scenes; ++s) {                         /* scenes and lifted references, single-threaded, arena 0 */
        if (s % 16 == 15) fill();
        tfn[s] = pick(funcs); scene(&tctx_in[s], tfn[s], 0); memcpy(tsave + (size_t)s * SPAN, g_xram, SPAN);
        xv_native_effects_force(0, 0); xctx c = tctx_in[s]; fn_of(tfn[s])(&c); tctx_ref[s] = c;
        tref[s] = malloc(SPAN); memcpy(tref[s], g_xram, SPAN); memcpy(g_xram, tsave + (size_t)s * SPAN, SPAN);
    }
    const char *m = getenv("NX_MODE"); xv_native_effects_force(m ? atoi(m) : 2, 0);
    pthread_t t[64]; tj j[64];
    for (unsigned i = 0; i < n && i < 64; ++i) { j[i] = (tj){ i + 1, scenes, iters, 0, 0, funcs }; pthread_create(&t[i], 0, tmain, &j[i]); }
    unsigned bad = 0, calls = 0;
    for (unsigned i = 0; i < n && i < 64; ++i) { pthread_join(t[i], 0); bad += j[i].bad; calls += j[i].calls; }
    printf("threads %u x %u iterations x %u scenes (mode %s): %u calls, %u mismatches\n", n, iters, scenes, m ? m : "2", calls, bad);
    return bad ? 1 : 0;
}
