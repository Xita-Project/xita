/* Differential test: native f_00070110 (recomp/kernel/xk_native_70110.c) against the lifted guest body of f_00070110,
 * extracted from a stage's shard and patched like the stage (wrapper, renamed body, tapped copy) by
 * tools/test_native_70110.py. Every callee is a deterministic stand-in whose effects are a hash of everything it can
 * see (all registers, the whole lazy-flag record, the x87 slots/top/status/control word, the budget, 64 bytes of stack
 * above esp, the call's sequence number): it clobbers eax/ecx/edx, sets a random flag record, pushes a float result
 * (B5130, 173F20, 118D0), scribbles dead x87 slots and its own frame below esp, writes its output buffers (111A0,
 * 56F20), spends back-edge budget and pops its arguments (`ret N` as the real one). The D3D HLE stand-ins record what
 * the real ones read (ecx, edx, esp, the return address and the stack arguments) and do what they change (esp, eax for
 * SetVertexShaderConstant, the texture-state word of SetTextureState_Deferred). The XV_MODEL_UV / XV_MODEL_FOG memo
 * stand-ins (hit or miss by the hash; a hit rewrites registers, flags, x87 and frame words like a replay would) and
 * the real XV_NATIVE_MATERIAL_SAMPLER sequence (xk_material_sampler.h) run in the builds with those flags.
 * Scenes: random materials, render contexts, lights, fog and camera globals (special values: NaN, inf, huge, zero,
 * signed zero, denormals), every branch of the body (the prologue's decline paths, the z-enable bit, the stage-mask and
 * alpha tests, the 621E0 path, the third vertex constant, the fog memo, the second pass, the 736F0 call), random entry
 * state (flags, x87 slots, an unmasked top, the budget about to run out), the frame window inside one page or across a
 * page end (the generic instance), shuffled tag pages (records across page ends), reversed stack pages.
 * Each case runs the guest body (mode 0), the native (mode 2) and verify mode (1) from identical state and compares the
 * stack, tag and image pages, every xctx field (NaN words: NaN against NaN), the callee sequence with each call's view
 * hash, the HLE calls, the memo calls and the xv_preempt calls; verify mode must leave the guest's result and report no
 * mismatch. --bench N: game-like scenes, guest vs native ns/call and user instructions/cycles (Linux perf counters),
 * both with the same cheap stand-ins. --threads N [iters]: N host threads run the native on the same scene through
 * their own page tables (private stack pages) at the same time (N70_MODE 2 or 1). */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/syscall.h>
#endif
#include "xv_x86rt.h"
#include "xv_phase.h"
#include "xk_material_sampler.h"

uint8_t *g_xram; uint32_t *g_xpt; uint8_t *g_img_base;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
__thread uint32_t *xv_host_page_table;
#endif
static __thread unsigned phase_begins, phase_ends;
int xv_phase_enabled = 1;

void xv_phase_begin(xv_phase_scope *s, void *c, unsigned id) { (void)c; phase_begins += id == 24; s->owner = s; }
void xv_phase_end(xv_phase_scope *s) { phase_ends++; s->owner = NULL; }
static unsigned log_mismatch; static int log_all;
void xk_os_log(const char *fmt, ...)
{
    char line[600]; va_list ap; va_start(ap, fmt); vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    if (strstr(line, "MISMATCH")) { if (++log_mismatch <= 8) printf("  log: %s", line); }
    else if (log_all || getenv("N70_DEBUG")) printf("%s", line);
}
uint64_t xk_os_monotonic_us(void) { return 0; }
static __thread unsigned preempt_calls; static int preempt_refill = 7;
void xv_preempt(xctx *c) { preempt_calls++; c->preempt = preempt_refill; }
void xv_trap(xctx *c, uint32_t eip) { (void)c; fprintf(stderr, "trap %08X\n", eip); abort(); }
volatile uint32_t xv_cur_fn; int xv_hle_timing; unsigned xv_hle_timed_calls;
void xv_hle_time_add(const char *n, uint64_t us) { (void)n; (void)us; }
#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
const char xv_object_job_marker = 0;
void xv_object_job_hle(xctx *c, unsigned a, void (*fn)(xctx *)) { (void)a; fn(c); }
#endif
void x_guest_read_pages(void *dst, uint32_t a, size_t size)
{
    uint8_t *d = dst;
    while (size) { size_t n = 4096u - (a & 0xFFFu); if (n > size) n = size; memcpy(d, X_G(a), n); d += n; a += (uint32_t)n; size -= n; }
}
void x_guest_write_pages(uint32_t a, const void *src, size_t size)
{
    const uint8_t *s = src;
    while (size) { size_t n = 4096u - (a & 0xFFFu); if (n > size) n = size; memcpy(X_G(a), s, n); s += n; a += (uint32_t)n; size -= n; }
}
static uint32_t r32(uint32_t a) { uint32_t v; x_guest_read_pages(&v, a, 4); return v; }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void w16(uint32_t a, uint16_t v) { x_guest_write_pages(a, &v, 2); }
static void w8(uint32_t a, uint8_t v) { x_guest_write_pages(a, &v, 1); }

extern void f_00070110(xctx *);
extern void xv_native_70110_force(int);
extern void xv_native_70110_report(unsigned);
extern unsigned xv_native_70110_mismatch_total(void);

/* ---- the run's record: every callee call (id + view hash), HLE call and memo call, in order ------------------- */
typedef struct { uint32_t kind, a, b, c, d, e, f, g; } ev;
enum { EV_MAX = 256 };
typedef struct { ev e[EV_MAX]; unsigned n; uint64_t seq; } evlog;
static __thread evlog *LOG;
static __thread int light;          /* --bench: no recording, cheap hash */
static void emit(uint32_t kind, uint32_t a, uint32_t b, uint32_t c_, uint32_t d, uint32_t e_, uint32_t f, uint32_t g)
{
    if (light || !LOG) return;
    if (LOG->n < EV_MAX) { ev *x = &LOG->e[LOG->n]; x->kind = kind; x->a = a; x->b = b; x->c = c_; x->d = d; x->e = e_; x->f = f; x->g = g; }
    LOG->n++;
}
static uint64_t mix(uint64_t h, uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); return h * 0xFF51AFD7ED558CCDull; }
/* everything a callee can see */
static uint64_t view_hash(const xctx *c, uint32_t id)
{
    uint64_t h = mix(0x51ED270B27u, id);
    if (LOG) h = mix(h, LOG->seq++);
    for (int i = 0; i < 8; ++i) h = mix(h, c->r[i]);
    if (light) return h;
    h = mix(h, c->f_kind); h = mix(h, c->f_op1); h = mix(h, c->f_op2); h = mix(h, c->f_res); h = mix(h, c->f_bits);
    h = mix(h, c->f_cf_override); h = mix(h, c->f_cf); h = mix(h, c->f_of_override); h = mix(h, c->f_of);
    h = mix(h, c->fsp); h = mix(h, c->fsw); h = mix(h, c->fcw); h = mix(h, (uint32_t)c->preempt);
    for (int i = 0; i < 8; ++i) { uint64_t b; memcpy(&b, &c->st[i], 8); if ((b & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (b << 12)) b = 0x7FF8u; h = mix(h, b); }
    for (uint32_t o = 0; o < 64; o += 4) { uint32_t w = r32(c->r[4] + o); if ((w & 0x7F800000u) == 0x7F800000u && (w & 0x7FFFFFu)) w = 0x7FC0u; h = mix(h, w); }
    return h;
}
static double hfloat(uint64_t h)
{
    switch (h % 16) {
    case 0: { uint64_t b = 0x7FF8000000000000ull | (h >> 20); double d; memcpy(&d, &b, 8); return d; }
    case 1: return (h & 64) ? INFINITY : -INFINITY;
    case 2: return (h & 64) ? 0.0 : -0.0;
    case 3: return (double)(float)((int32_t)(h >> 16) * 1e30);
    default: return (double)(float)((int32_t)(h >> 24) / 65536.0);
    }
}
static uint32_t hf32(uint64_t h) { float f = (float)hfloat(h); uint32_t u; memcpy(&u, &f, 4); return u; }
/* a random, internally consistent lazy-flag record */
static void random_flags(xctx *c, uint64_t h)
{
    static const uint32_t bits[3] = { 8, 16, 32 };
    c->f_kind = (uint32_t)(h % 6); c->f_op1 = (uint32_t)(h >> 7); c->f_op2 = (uint32_t)(h >> 19); c->f_res = (uint32_t)(h >> 29);
    c->f_bits = bits[(h >> 40) % 3]; c->f_cf_override = (h >> 43) & 1; c->f_cf = (h >> 44) & 1; c->f_of_override = (h >> 45) & 1; c->f_of = (h >> 46) & 1;
}

/* ---- translated-callee stand-ins ------------------------------------------------------------------------------- */
static void callee(xctx *c, uint32_t id, uint32_t ret, int fret, uint32_t out, unsigned outn)
{
    if (light) {                                   /* --bench: as cheap as a stand-in can be (the same on both sides) */
        c->r[0] = id; c->r[1] = 0x1234u; c->r[2] = 0; if (id == 0x621E0u) c->r[0] = 1;
        if (fret) { c->fsp = (c->fsp - 1u) & 7u; c->st[c->fsp] = 1.0; }
        c->r[4] += 4u + ret; return;
    }
    const uint64_t h = view_hash(c, id);
    emit(1, id, (uint32_t)h, (uint32_t)(h >> 32), c->r[4], r32(c->r[4]), 0, 0);
    uint64_t k = mix(h, 1);
    c->r[0] = (uint32_t)k; c->r[1] = (uint32_t)(k >> 32); k = mix(k, 2); c->r[2] = (uint32_t)k;
    if (id == 0x621E0u) c->r[0] = (c->r[0] & 0xFFFF0000u) | (uint16_t)((k >> 40) % 7 - 1);   /* a small index in ax */
    if (!light) {
        random_flags(c, mix(k, 3));
        uint32_t T = c->fsp & 7u;
        for (unsigned i = 1; i <= (unsigned)((k >> 33) % 4); ++i) c->st[(T - i) & 7u] = hfloat(mix(k, 10 + i));   /* dead slots */
        if (fret) { c->fsp = (T - 1u) & 7u; c->st[c->fsp] = hfloat(mix(k, 20)); }
        else if ((k >> 50) % 8 == 0) c->fsp = T;                                       /* masks an unmasked top */
        if ((k >> 53) & 1) c->fsw = (uint16_t)((c->fsw & 0x3800u) | ((k >> 20) & 0x4700u));
        for (uint32_t o = 4; o <= 0x20; o += 4) w32(c->r[4] - o, (uint32_t)mix(k, 30 + o));   /* its frame below esp */
        const int zeros = (k >> 60) % 3 == 0;   /* all outputs 0.0 now and then (70720..70794 test three of them for 0.0) */
        for (unsigned i = 0; i < outn; ++i) {   /* output words: 0.0 and 1.0 often (the alpha and stage tests compare with them) */
            const uint64_t q = zeros ? 0 : mix(k, 40 + i);
            w32(out + 4u * i, q % 4 == 0 ? 0u : q % 4 == 1 ? 0x3F800000u : (i & 1) ? hf32(q >> 2) : (uint32_t)mix(k, 60 + i));
        }
        c->preempt -= (int32_t)((k >> 57) % 3);
    } else if (fret) { c->fsp = (c->fsp - 1u) & 7u; c->st[c->fsp] = 1.0; }
    c->r[4] += 4u + ret;
}
void f_0006EFC0(xctx *c) { callee(c, 0x6EFC0u, 0x20, 0, 0, 0); }
void f_0005FD40(xctx *c) { callee(c, 0x5FD40u, 0, 0, 0, 0); }
void f_0006F860(xctx *c) { callee(c, 0x6F860u, 0x18, 0, 0, 0); }
void f_00080360(xctx *c) { callee(c, 0x80360u, 0x10, 0, 0, 0); }
void f_000B5130(xctx *c) { callee(c, 0xB5130u, 0, 1, 0, 0); }
void f_00173F20(xctx *c) { callee(c, 0x173F20u, 8, 1, 0, 0); }
void f_000111A0(xctx *c) { callee(c, 0x111A0u, 4, 0, c->r[0], 3); }
void f_000621E0(xctx *c) { callee(c, 0x621E0u, 0, 0, 0, 0); }
void f_000658D0(xctx *c) { callee(c, 0x658D0u, 0xC, 0, 0, 0); }
void f_00056F20(xctx *c) { uint32_t o = c->r[3]; callee(c, 0x56F20u, 0x18, 0, o, 8); }   /* ebx -> [esp+5C..]: the rows the VSC call reads */
void f_000118D0(xctx *c) { callee(c, 0x118D0u, 0, 1, 0, 0); }
void f_00011B60(xctx *c) { callee(c, 0x11B60u, 0, 0, 0, 0); }
void f_00011BD0(xctx *c) { callee(c, 0x11BD0u, 0, 0, 0, 0); }
void f_0006F340(xctx *c) { callee(c, 0x6F340u, 0x18, 0, 0, 0); }
void f_0007A960(xctx *c) { callee(c, 0x7A960u, 8, 0, 0, 0); }
void f_000736F0(xctx *c) { callee(c, 0x736F0u, 0x18, 0, 0, 0); }

/* ---- D3D HLE stand-ins: record what the real ones read, do what they change ------------------------------------ */
static void hle(xctx *c, uint32_t kind, unsigned nargs)
{
    const uint32_t sp = c->r[4];
    if (light) { if (kind == 6) c->r[0] = 0; c->r[4] = sp + 4u + 4u * nargs; return; }
    emit(2, kind, c->r[1], c->r[2], sp, r32(sp), nargs > 0 ? r32(sp + 4) : 0, nargs > 2 ? r32(sp + 8) ^ (r32(sp + 12) << 1) : 0);
    if (kind == 5 && (c->r[1] & 3u) == c->r[1] && c->r[2] < 32u) w32(0x18F180u + ((c->r[1] << 5) + c->r[2]) * 4u, r32(sp + 4));
    if (kind == 6) { c->r[0] = 0; if (!light) { uint32_t s = r32(sp + 8), n = r32(sp + 12); for (uint32_t i = 0; i < 4 * (n > 4 ? 4 : n); ++i) { uint32_t w = r32(s + 4 * i); if ((w & 0x7F800000u) == 0x7F800000u && (w & 0x7FFFFFu)) w = 0x7FC0u; emit(3, s + 4 * i, w, 0, 0, 0, 0, 0); } } }   /* NaN words canonical: the real one zeroes non-finite components */
    c->r[4] = sp + 4u + 4u * nargs;
}
void xv_hle_D3DDevice_SetRenderState_ZEnable(xctx *c) { hle(c, 1, 1); }
void xv_hle_D3DDevice_SetRenderState_Simple(xctx *c) { hle(c, 2, 0); }
void xv_hle_D3DDevice_SetRenderState_ZBias(xctx *c) { hle(c, 3, 1); }
void xv_hle_D3DDevice_SetRenderState_CullMode(xctx *c) { hle(c, 4, 1); }
void xv_hle_D3DDevice_SetTextureState_Deferred(xctx *c) { hle(c, 5, 1); }
void xv_hle_D3DDevice_SetVertexShaderConstant(xctx *c) { hle(c, 6, 3); }

/* ---- the lift's optional hooks (builds with XV_MODEL_UV / XV_MODEL_FOG / XV_NATIVE_MATERIAL_SAMPLER) ---------- */
static int hook_args_bad;
static void check_args(const uint8_t *ram, const uint32_t *pt, const uint8_t *img)
{
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE
    if (ram != g_xram || pt != X_PT || img != X_IMG_BASE) hook_args_bad++;
#else
    if (ram != g_xram || pt != g_xpt || img != g_img_base) hook_args_bad++;
#endif
}
int xk_model_uv_begin(xctx *c, const uint8_t *ram, const uint32_t *pt, const uint8_t *img, unsigned variant, unsigned *tok)
{
    if (light) return 0;                      /* --bench: the scene helper's answer (owner-only memos) */
    check_args(ram, pt, img);
    const uint64_t h = view_hash(c, 0x100u + variant); *tok = (unsigned)(h >> 8);
    emit(4, variant, (uint32_t)h, *tok, c->r[4], 0, 0, 0);
    if (!(h & 1)) return 0;
    callee(c, 0x56F21u, 0x18, 0, c->r[3], 8);           /* a hit: the recorded effect of 56F20 */
    return 1;
}
void xk_model_uv_end(xctx *c, unsigned tok) { if (light) return; emit(5, tok, (uint32_t)view_hash(c, 0x110u), c->r[0], c->r[4], 0, 0, 0); }
int xk_model_fog_begin(xctx *c, const uint8_t *ram, const uint32_t *pt, const uint8_t *img, unsigned *tok)
{
    if (light) return 0;
    check_args(ram, pt, img);
    const uint64_t h = view_hash(c, 0x120u); *tok = (unsigned)(h >> 4);
    emit(6, (uint32_t)h, *tok, c->r[4], 0, 0, 0, 0);
    if (!(h & 1)) return 0;
    uint64_t k = mix(h, 7);                              /* a hit: the region's outputs, as a replay leaves them */
    c->r[0] = (uint32_t)k; c->r[1] = (uint32_t)(k >> 32); k = mix(k, 8); c->r[2] = (uint32_t)k; c->r[3] = (uint32_t)(k >> 32);
    k = mix(k, 9); c->r[6] = (uint32_t)k; c->r[7] = (uint32_t)(k >> 32);
    random_flags(c, mix(k, 10));
    c->st[(c->fsp - 1u) & 7u] = hfloat(mix(k, 11)); c->fsw = (uint16_t)((c->fsw & 0x3800u) | ((k >> 24) & 0x4700u));
    for (uint32_t o = 0x10; o <= 0x2C; o += 4) w32(c->r[4] + o, hf32(mix(k, 20 + o)));
    for (uint32_t o = 0xA4; o <= 0xAC; o += 4) w32(c->r[4] + o, hf32(mix(k, 40 + o)));
    return 1;
}
void xk_model_fog_end(xctx *c, unsigned tok) { if (light) return; emit(7, tok, (uint32_t)view_hash(c, 0x130u), c->r[4], 0, 0, 0, 0); }
int xv_material_sampler_try(xctx *c, unsigned stage)
{
    if (light) return 0;
    const uint64_t h = view_hash(c, 0x140u + stage);
    emit(8, stage, (uint32_t)h, c->r[4], 0, 0, 0, 0);
    if (!(h & 1)) return 0;
    xv_material_sampler_defaults(c, stage);
    return 1;
}

/* ---- arena: image pages at their flat offsets (X_IMG and the table agree), shuffled tag pages, reversed stack ---- */
enum { IMG_LO = 0x18F000u, IMG_HI = 0x2FD000u, IMG_BYTES = IMG_HI - IMG_LO, TAG = 0x40000000u, TAG_PAGES = 24,
       STACK = 0xD0000000u, STACK_PAGES = 4 };
static const uint32_t IMG_USED[] = { 0x18F000u, 0x1F0000u, 0x232000u, 0x2E3000u, 0x2FC000u };
enum { N_IMG_USED = 5 };
static uint32_t trash_off, stack_off, arena_bytes;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static float fb(uint32_t b) { float f; memcpy(&f, &b, 4); return f; }
static float special(void)
{
    switch (rnd() % 8) {
    case 0: return fb(0x7FC00000u | (rnd() & 0x3FFFFF));
    case 1: return fb(0xFFC00000u | (rnd() & 0x3FFFFF));
    case 2: return INFINITY;
    case 3: return -INFINITY;
    case 4: return rnd() & 1 ? 3.0e38f : -3.0e38f;
    case 5: return fb(rnd() & 0x807FFFFFu);                  /* denormal */
    default: return rnd() & 1 ? 0.0f : -0.0f;
    }
}
static unsigned special_rate = 30;   /* per mille */
static float fv(float lo, float hi) { return rnd() % 1000 < special_rate ? special() : frand(lo, hi); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }

static void map_memory(void)
{
    const uint32_t img_off = 0;
    stack_off = IMG_BYTES + (TAG_PAGES << 12);
    trash_off = stack_off + (STACK_PAGES << 12) * 17u;       /* room for 16 threads' private stacks */
    arena_bytes = trash_off + 4096u;
    g_xram = calloc(1, arena_bytes); g_xpt = malloc(sizeof(uint32_t) << 20);
    for (uint32_t i = 0; i < (1u << 20); ++i) g_xpt[i] = trash_off;
    for (uint32_t p = IMG_LO; p < IMG_HI; p += 4096u) g_xpt[p >> 12] = img_off + (p - IMG_LO);
    g_img_base = g_xram + img_off - IMG_LO;
    uint32_t perm[TAG_PAGES];
    for (uint32_t i = 0; i < TAG_PAGES; ++i) perm[i] = i;
    for (uint32_t i = TAG_PAGES - 1; i > 0; --i) { uint32_t j = rnd() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (uint32_t i = 0; i < TAG_PAGES; ++i) g_xpt[(TAG >> 12) + i] = IMG_BYTES + (perm[i] << 12);
    for (uint32_t i = 0; i < STACK_PAGES; ++i) g_xpt[(STACK >> 12) + i] = stack_off + ((STACK_PAGES - 1 - i) << 12);
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
}
/* the pages a case can touch: the used image pages, the tag pages, the stack pages */
enum { SNAP_BYTES = (N_IMG_USED + TAG_PAGES + STACK_PAGES) * 4096 };
static void snap(uint8_t *d)
{
    for (int i = 0; i < N_IMG_USED; ++i) x_guest_read_pages(d + i * 4096, IMG_USED[i], 4096);
    x_guest_read_pages(d + N_IMG_USED * 4096, TAG, TAG_PAGES << 12);
    x_guest_read_pages(d + (N_IMG_USED + TAG_PAGES) * 4096, STACK, STACK_PAGES << 12);
}
static void unsnap(const uint8_t *d)
{
    for (int i = 0; i < N_IMG_USED; ++i) x_guest_write_pages(IMG_USED[i], d + i * 4096, 4096);
    x_guest_write_pages(TAG, d + N_IMG_USED * 4096, TAG_PAGES << 12);
    x_guest_write_pages(STACK, d + (N_IMG_USED + TAG_PAGES) * 4096, STACK_PAGES << 12);
}
static uint32_t snap_addr(unsigned off)
{
    if (off < N_IMG_USED * 4096u) return IMG_USED[off >> 12] + (off & 0xFFFu);
    off -= N_IMG_USED * 4096u; if (off < (TAG_PAGES << 12)) return TAG + off;
    return STACK + off - (TAG_PAGES << 12);
}
static uint32_t tag_top;
static uint32_t tag_alloc(uint32_t bytes)
{
    uint32_t a = (tag_top + 3u) & ~3u;
    if (rnd() % 7 == 0) { uint32_t end = (a | 0xFFFu) + 1u; if (end - a < bytes && end - a > 8) a = end - 4u * (1 + rnd() % 8); }   /* across a page end */
    if (rnd() % 23 == 0) a += 1 + rnd() % 3;                                                                                     /* unaligned */
    tag_top = a + bytes;
    if (tag_top > TAG + (TAG_PAGES << 12) - 64) { fprintf(stderr, "tag space exhausted\n"); exit(2); }
    return a;
}

/* ---- scenes ---------------------------------------------------------------------------------------------------- */
static int game_like;   /* --bench: the common path (no declines, no special values) */
typedef struct { uint32_t E; } scene_t;
static void scene(scene_t *s)
{
    memset(g_xram + IMG_BYTES, 0, TAG_PAGES << 12);
    tag_top = TAG + 16 + rnd() % 256;
    special_rate = game_like ? 0 : (rnd() % 4 == 0 ? 250 : 20);
    /* image constants */
    wf(0x1F0A68u, game_like || rnd() % 10 ? 0.0f : fv(-1, 1)); wf(0x1F0A78u, game_like || rnd() % 10 ? 1.0f : fv(0, 2));
    for (uint32_t a = 0x2FC6C8u; a <= 0x2FC6DCu; a += 4) wf(a, fv(-100, 100));
    w8(0x2FC8A8u, (uint8_t)rnd());
    for (uint32_t a = 0x2FC8ACu; a <= 0x2FC8E0u; a += 4) wf(a, fv(-2, 2));
    wf(0x2FC8BCu, fv(0, 50)); wf(0x2FC8C0u, fv(10, 500)); wf(0x2FC918u, fv(0.5f, 2));
    /* the render context S = [2E3520] */
    const uint32_t S = tag_alloc(0xD0);
    for (uint32_t o = 0; o < 0xD0; o += 4) w32(S + o, rnd());
    w8(S, (uint8_t)(game_like ? rnd() & ~8u : rnd()));
    for (uint32_t o = 0xB4; o <= 0xBC; o += 4) wf(S + o, fv(-100, 100));
    for (uint32_t o = 0x5C; o <= 0x68; o += 4) wf(S + o, fv(0, 2));
    wf(S + 0xC4, fv(0, 2)); wf(S + 0xC8, fv(0, 2));
    w16(S + 0x50, (uint16_t)(rnd() % 3 - 1)); w16(S + 0x0C, (uint16_t)(rnd() % 3));
    uint32_t lights = tag_alloc(5 * 12);
    if (rnd() % 8 == 0) { tag_top = ((tag_top | 0xFFFu) + 1u) + 0x40u; lights = tag_top - 0x40u - 1u - rnd() % 0x30u; tag_top = lights + 60u; }   /* unaligned, across a page end */
    for (uint32_t o = 0; o < 60; o += 4) wf(lights + o, rnd() % 3 == 0 ? 1.0f : fv(0, 1));
    w32(S + 0x84, lights);
    w32(S + 0xA8, 0);
    if (!game_like && rnd() % 12 == 0) {                            /* the prologue's 6EFC0 path (decline) or its skip */
        const uint32_t x = tag_alloc(0x30), fl = tag_alloc(16);
        w16(x + 0x24, (uint16_t)(rnd() % 3 ? 0xA : rnd() % 12)); w16(x + 0x2C, (uint16_t)(rnd() % 6));
        for (uint32_t o = 0; o < 16; o += 4) wf(fl + o, rnd() % 3 == 0 ? 0.0f : rnd() % 2 ? 1.0f : fv(-1, 1));
        w32(S + 0xA8, x); w32(S + 0xB0, rnd() % 5 ? fl : 0);
    }
    w32(0x2E3520u, S);
    w16(0x2E3528u, (uint16_t)(game_like ? 0 : rnd() % 40 == 0 ? 1 : rnd() % 40 == 0 ? 2 + rnd() % 3 : 0));
    w8(0x2E352Bu, (uint8_t)(rnd() % 4 == 0)); w8(0x2E352Cu, (uint8_t)(rnd() % 3 == 0));
    if (rnd() % 3) { const uint32_t v = tag_alloc(16); for (uint32_t o = 0; o < 16; o += 4) wf(v + o, rnd() % 3 ? 0.0f : fv(-1, 1)); w32(0x2E3508u, v); }
    else w32(0x2E3508u, 0);
    { const uint32_t v = tag_alloc(12); for (uint32_t o = 0; o < 12; o += 4) wf(v + o, rnd() % 2 ? 1.0f : fv(0, 1)); w32(0x232F6Cu, v); }
    /* the material */
    uint32_t M = tag_alloc(0x180);
    if (rnd() % 6 == 0) { tag_top = ((tag_top | 0xFFFu) + 1u) + 0x100u; M = tag_top - 0x100u - 4u * (1 + rnd() % 0x5F) - (rnd() % 3 ? 0 : 1 + rnd() % 3); tag_top = M + 0x180u; }   /* across a page end (sometimes unaligned: split float loads) */
    for (uint32_t o = 0; o < 0x180; o += 4) w32(M + o, rnd());
    { static const uint16_t kinds[4] = { 0, 1, 2, 4 }; w16(M + 0x24, game_like ? kinds[rnd() % 3] : rnd() % 10 == 0 ? 3 : kinds[rnd() % 4]); }
    w8(M + 0x28, (uint8_t)rnd());
    w16(M + 0x4C, (uint16_t)(rnd() % 7 - 1)); w16(M + 0x70, (uint16_t)(rnd() % 7 - 1));
    wf(M + 0x74, fv(0.1f, 10)); for (uint32_t o = 0x78; o <= 0x8C; o += 4) wf(M + o, fv(-10, 10));
    wf(M + 0x9C, fv(0, 2)); wf(M + 0xA0, fv(0, 2)); wf(M + 0xD8, fv(0, 2)); wf(M + 0xEC, fv(0, 2));
    for (uint32_t o = 0x13C; o <= 0x160; o += 4) wf(M + o, fv(-2, 2));
    if (rnd() % 3 == 0) wf(M + 0x140, 0.0f);
    w32(M + 0xC8, rnd() % 2 ? 0xFFFFFFFFu : rnd()); w32(M + 0x170, rnd() % 2 ? 0xFFFFFFFFu : rnd());
    w16(M + 0xD6, (uint16_t)(rnd() % 3 ? 0 : rnd()));
    /* the stack: E inside the pages, the window [E - 0x100, E + 0x20) sometimes across a page end */
    uint32_t E;
    do {
        E = STACK + 0x400u + ((rnd() % ((STACK_PAGES << 12) - 0x800u)) & ~3u);
        if (!game_like && rnd() % 5 == 0) E = ((E | 0xFFFu) + 1u) - 4u * (rnd() % 0x48u);   /* the window across a page end */
        if (game_like) E = (E & ~0xFFFu) + 0x400u + 4u * (rnd() % 0x300u);
    } while (E > STACK + (STACK_PAGES << 12) - 0x40u || E < STACK + 0x200u);
    if (!game_like && rnd() % 100 == 0) E += 1 + rnd() % 3;   /* esp not 4-aligned: the native declines (layout) */
    for (uint32_t o = 0; o < 0x400; o += 4) w32(E - 0x200u + o, rnd());
    w32(E + 4, M); w32(E + 8, rnd());
    if (rnd() % 3) { const uint32_t p = tag_alloc(4); w16(p, (uint16_t)rnd()); w32(E + 0x18, p); } else w32(E + 0x18, 0);
    s->E = E;
}
static void init_ctx(xctx *c, const scene_t *s)
{
    memset(c, 0, sizeof *c);
    for (int i = 0; i < 8; ++i) c->r[i] = rnd();
    c->r[4] = s->E;
    const uint64_t h = ((uint64_t)rnd() << 32) | rnd(); random_flags(c, h);
    for (int i = 0; i < 8; ++i) c->st[i] = (rnd() % 9 == 0) ? (double)special() : (double)frand(-1000, 1000);
    c->fsp = game_like ? rnd() % 8 : rnd() % 16;
    c->fsw = (uint16_t)rnd(); c->fcw = rnd() % 3 ? 0x037Fu : (uint16_t)rnd();
    c->preempt = game_like ? 1 << 30 : rnd() % 3 == 0 ? (int32_t)(rnd() % 4) : 20000;
    c->fs_base = rnd(); c->scratch = rnd(); c->eip_hint = rnd();
    for (int i = 0; i < 8; ++i) c->mm[i] = ((uint64_t)rnd() << 32) | rnd();
}

/* ---- one run and the comparison -------------------------------------------------------------------------------- */
typedef struct { xctx c; evlog log; unsigned preempts, phase_b, phase_e; } result;
static void run(int mode, const xctx *c0, result *r)
{
    evlog *saved = LOG; LOG = &r->log; memset(&r->log, 0, sizeof r->log);
    preempt_calls = 0; phase_begins = phase_ends = 0;
    xv_native_70110_force(mode);
    r->c = *c0; f_00070110(&r->c);
    xv_native_70110_force(0);
    r->preempts = preempt_calls; r->phase_b = phase_begins; r->phase_e = phase_ends;
    LOG = saved;
}
static int nan64(uint64_t b) { return (b & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (b << 12); }
static int nan32w(uint32_t w) { return (w & 0x7F800000u) == 0x7F800000u && (w & 0x7FFFFFu); }
static unsigned nan_words;
static int same(const result *a, const result *b, const uint8_t *ma, const uint8_t *mb, char *why, size_t n, int hle_full)
{
    for (int i = 0; i < 8; ++i) if (a->c.r[i] != b->c.r[i]) { if (nan32w(a->c.r[i]) && nan32w(b->c.r[i])) { nan_words++; continue; } snprintf(why, n, "r[%d] %08X vs %08X", i, a->c.r[i], b->c.r[i]); return 0; }
#define CF(f) if (a->c.f != b->c.f) { snprintf(why, n, #f " %08X vs %08X", (uint32_t)a->c.f, (uint32_t)b->c.f); return 0; }
    CF(f_kind) CF(f_op1) CF(f_op2) CF(f_res) CF(f_bits) CF(f_cf_override) CF(f_cf) CF(f_of_override) CF(f_of)
    CF(fsp) CF(fsw) CF(fcw) CF(preempt) CF(fs_base) CF(df) CF(scratch) CF(eip_hint)
#undef CF
    for (int i = 0; i < 8; ++i) { uint64_t x, y; memcpy(&x, &a->c.st[i], 8); memcpy(&y, &b->c.st[i], 8);
        if (x != y) { if (nan64(x) && nan64(y)) { nan_words++; continue; } snprintf(why, n, "st[%d] %016llX vs %016llX", i, (unsigned long long)x, (unsigned long long)y); return 0; } }
    if (memcmp(a->c.mm, b->c.mm, sizeof a->c.mm) || memcmp(a->c.xmm, b->c.xmm, sizeof a->c.xmm)) { snprintf(why, n, "mm/xmm"); return 0; }
    if (a->preempts != b->preempts) { snprintf(why, n, "xv_preempt calls %u vs %u", a->preempts, b->preempts); return 0; }
    if (a->phase_b != b->phase_b || a->phase_e != b->phase_e) { snprintf(why, n, "phase scope %u/%u vs %u/%u", a->phase_b, a->phase_e, b->phase_b, b->phase_e); return 0; }
    if (a->log.n != b->log.n) { snprintf(why, n, "events %u vs %u", a->log.n, b->log.n); return 0; }
    for (unsigned i = 0; i < a->log.n && i < EV_MAX; ++i) {
        const ev *x = &a->log.e[i], *y = &b->log.e[i];
        if (x->kind != y->kind || x->a != y->a) { snprintf(why, n, "event %u kind %u/%u a %08X/%08X", i, x->kind, y->kind, x->a, y->a); return 0; }
        if (memcmp(x, y, sizeof *x) && (hle_full || x->kind != 2 || memcmp(x, y, sizeof *x))) {
            snprintf(why, n, "event %u (kind %u %08X): %08X %08X %08X %08X %08X %08X vs %08X %08X %08X %08X %08X %08X", i, x->kind, x->a,
                     x->b, x->c, x->d, x->e, x->f, x->g, y->b, y->c, y->d, y->e, y->f, y->g); return 0; }
    }
    for (unsigned o = 0; o < SNAP_BYTES; o += 4) {
        uint32_t x, y; memcpy(&x, ma + o, 4); memcpy(&y, mb + o, 4);
        if (x != y) { if (nan32w(x) && nan32w(y)) { nan_words++; continue; } snprintf(why, n, "memory %08X %08X vs %08X", snap_addr(o), x, y); return 0; }
    }
    return 1;
}

/* ---- a SIGPROF sampler for --bench (N70_PROF=<file>: one PC per line, the native's runs only unless N70_PROF_GUEST) */
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
#include <fcntl.h>
#include <sys/ioctl.h>
static volatile int prof_on; static uintptr_t *prof_pc; static unsigned prof_n, prof_cap = 1u << 22; static int prof_fd = -1;
static void on_prof(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)si;
    if (prof_fd >= 0) ioctl(prof_fd, PERF_EVENT_IOC_REFRESH, 1);
    if (!prof_on || prof_n >= prof_cap) return;
#if defined(__x86_64__)
    prof_pc[prof_n++] = (uintptr_t)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP];
#elif defined(__arm__)
    prof_pc[prof_n++] = (uintptr_t)((ucontext_t *)uc)->uc_mcontext.arm_pc;
#endif
}
static void prof_start(void)
{
    if (!getenv("N70_PROF")) return;
    prof_pc = malloc(sizeof *prof_pc * prof_cap);
    struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGPROF, &sa, NULL);
#if defined(__linux__)
    const char *cyc = getenv("N70_PROF_CYCLES");   /* sample every N user cycles (perf_event overflow), else ITIMER_PROF */
    if (cyc) {
        struct perf_event_attr at; memset(&at, 0, sizeof at); at.size = sizeof at; at.type = PERF_TYPE_HARDWARE;
        at.config = PERF_COUNT_HW_CPU_CYCLES; at.exclude_kernel = 1; at.exclude_hv = 1; at.sample_period = strtoull(cyc, 0, 0);
        at.wakeup_events = 1; at.disabled = 1;
        prof_fd = (int)syscall(__NR_perf_event_open, &at, 0, -1, -1, 0);
        if (prof_fd >= 0) {
            struct f_owner_ex owner = { F_OWNER_TID, (pid_t)syscall(SYS_gettid) };
            fcntl(prof_fd, F_SETFL, O_ASYNC | O_NONBLOCK); fcntl(prof_fd, F_SETSIG, SIGPROF); fcntl(prof_fd, F_SETOWN_EX, &owner);
            ioctl(prof_fd, PERF_EVENT_IOC_RESET, 0); ioctl(prof_fd, PERF_EVENT_IOC_REFRESH, 1);
            return;
        }
    }
#endif
    struct itimerval it = { { 0, 97 }, { 0, 97 } }; setitimer(ITIMER_PROF, &it, NULL);
}
static void prof_stop(void)
{
    const char *prof = getenv("N70_PROF"); if (!prof) return;
    struct itimerval z; memset(&z, 0, sizeof z); setitimer(ITIMER_PROF, &z, NULL);
    if (prof_fd >= 0) { ioctl(prof_fd, PERF_EVENT_IOC_DISABLE, 0); close(prof_fd); prof_fd = -1; }
    FILE *pf = fopen(prof, "w");
    if (pf) { for (unsigned i = 0; i < prof_n; ++i) fprintf(pf, "%lx\n", (unsigned long)prof_pc[i]); fclose(pf); }
    printf("profile: %u samples to %s\n", prof_n, prof);
}
/* ---- perf counters (--bench) ------------------------------------------------------------------------------------ */
static int pe_fd[2] = { -1, -1 };
static void pe_open(void)
{
#if defined(__linux__)
    for (int i = 0; i < 2; ++i) {
        struct perf_event_attr a; memset(&a, 0, sizeof a); a.size = sizeof a; a.type = PERF_TYPE_HARDWARE;
        a.config = i ? PERF_COUNT_HW_CPU_CYCLES : PERF_COUNT_HW_INSTRUCTIONS; a.exclude_kernel = 1; a.exclude_hv = 1;
        pe_fd[i] = (int)syscall(__NR_perf_event_open, &a, 0, -1, -1, 0);
    }
#endif
}
static void pe_read(uint64_t v[2]) { for (int i = 0; i < 2; ++i) { v[i] = 0; if (pe_fd[i] >= 0 && read(pe_fd[i], &v[i], 8) != 8) v[i] = 0; } }

/* ---- --threads -------------------------------------------------------------------------------------------------- */
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
typedef struct { uint32_t *pt; int iters, mode; xctx c0; const result *ref; const uint8_t *init, *refstack; unsigned bad, runs; char why[240]; } th_arg;
static void *th_main(void *p)
{
    th_arg *a = p;
    xv_host_page_table = a->pt;
    result *r = malloc(sizeof *r); uint8_t *cur = malloc((size_t)STACK_PAGES << 12);
    for (int k = 0; k < a->iters; ++k) {
        x_guest_write_pages(STACK, a->init, (size_t)STACK_PAGES << 12);
        evlog *saved = LOG; LOG = &r->log; memset(&r->log, 0, sizeof r->log); preempt_calls = 0; phase_begins = phase_ends = 0;
        r->c = a->c0; f_00070110(&r->c);
        r->preempts = preempt_calls; r->phase_b = phase_begins; r->phase_e = phase_ends; LOG = saved;
        x_guest_read_pages(cur, STACK, (size_t)STACK_PAGES << 12);
        char why[240] = "";
        int ok = 1;
        /* registers, flags, x87, events (the stack pages separately: only they are private) */
        result x = *a->ref;
        uint8_t dummy[4] = { 0 };
        (void)dummy;
        if (memcmp(&r->c, &x.c, sizeof x.c)) {
            uint8_t z[SNAP_BYTES / 64]; (void)z;
            ok = 0; snprintf(why, sizeof why, "context differs (eax %08X/%08X esp %08X/%08X)", r->c.r[0], x.c.r[0], r->c.r[4], x.c.r[4]);
            for (int i = 0; i < 8; ++i) { uint64_t u, v; memcpy(&u, &r->c.st[i], 8); memcpy(&v, &x.c.st[i], 8); if (u != v && nan64(u) && nan64(v)) { r->c.st[i] = x.c.st[i]; } }
            if (!memcmp(&r->c, &x.c, sizeof x.c)) { ok = 1; why[0] = 0; }
        }
        if (ok && (r->log.n != x.log.n || memcmp(r->log.e, x.log.e, sizeof(ev) * (r->log.n < EV_MAX ? r->log.n : EV_MAX)) || r->preempts != x.preempts)) {
            /* HLE records in mode 2 hold the registers the HLE reads; the reference was made in the same mode */
            ok = 0; snprintf(why, sizeof why, "events differ (%u vs %u)", r->log.n, x.log.n);
        }
        if (ok) for (uint32_t i = 0; i < ((uint32_t)STACK_PAGES << 12); i += 4) {
            uint32_t u, v; memcpy(&u, cur + i, 4); memcpy(&v, a->refstack + i, 4);
            if (u != v && !(nan32w(u) && nan32w(v))) { ok = 0; snprintf(why, sizeof why, "stack %08X %08X vs %08X", STACK + i, u, v); break; }
        }
        if (!ok && !a->bad++) snprintf(a->why, sizeof a->why, "%s", why);
        a->runs++;
    }
    free(cur); free(r);
    return NULL;
}
static int threads_test(unsigned scenes, int nth, int iters)
{
    const int mode = getenv("N70_MODE") ? atoi(getenv("N70_MODE")) : 2;
    if (nth > 16) nth = 16;
    uint32_t **pts = malloc(sizeof *pts * (size_t)nth);
    for (int t = 0; t < nth; ++t) {
        pts[t] = malloc(sizeof(uint32_t) << 20); memcpy(pts[t], g_xpt, sizeof(uint32_t) << 20);
        for (uint32_t i = 0; i < STACK_PAGES; ++i) pts[t][(STACK >> 12) + i] = stack_off + (((uint32_t)(t + 1) * STACK_PAGES + (STACK_PAGES - 1 - i)) << 12);
    }
    uint8_t *init = malloc((size_t)STACK_PAGES << 12), *refstack = malloc((size_t)STACK_PAGES << 12);
    result *ref = malloc(sizeof *ref);
    unsigned bad = 0, runs = 0, badscenes = 0;
    for (unsigned k = 0; k < scenes; ++k) {
        xv_host_page_table = g_xpt;
        scene_t s; scene(&s); xctx c0; init_ctx(&c0, &s); c0.preempt = 1 << 30;
        x_guest_read_pages(init, STACK, (size_t)STACK_PAGES << 12);
        run(mode, &c0, ref);                                   /* the reference: this mode, single-threaded */
        x_guest_read_pages(refstack, STACK, (size_t)STACK_PAGES << 12);
        { result *g = malloc(sizeof *g); x_guest_write_pages(STACK, init, (size_t)STACK_PAGES << 12); run(0, &c0, g);   /* and the guest agrees */
          uint8_t *gs = malloc((size_t)STACK_PAGES << 12); x_guest_read_pages(gs, STACK, (size_t)STACK_PAGES << 12);
          int differs = 0;
          for (int i = 0; i < 8; ++i) if (g->c.r[i] != ref->c.r[i] && !(nan32w(g->c.r[i]) && nan32w(ref->c.r[i]))) differs = 1;
          for (uint32_t i = 0; i < ((uint32_t)STACK_PAGES << 12); i += 4) { uint32_t u, v; memcpy(&u, gs + i, 4); memcpy(&v, refstack + i, 4); if (u != v && !(nan32w(u) && nan32w(v))) differs = 1; }
          if (differs) { if (++badscenes <= 5) printf("scene %u: mode %d differs from the guest single-threaded\n", k, mode); bad++; }
          free(gs); free(g); }
        th_arg *a = calloc((size_t)nth, sizeof *a); pthread_t *th = malloc(sizeof *th * (size_t)nth);
        xv_native_70110_force(mode);
        for (int t = 0; t < nth; ++t) { a[t].pt = pts[t]; a[t].iters = iters; a[t].mode = mode; a[t].c0 = c0; a[t].ref = ref; a[t].init = init; a[t].refstack = refstack; pthread_create(&th[t], NULL, th_main, &a[t]); }
        for (int t = 0; t < nth; ++t) {
            pthread_join(th[t], NULL); runs += a[t].runs;
            if (a[t].bad) { if (++badscenes <= 10) printf("scene %u thread %d: %u of %u runs differ (%s)\n", k, t, a[t].bad, a[t].runs, a[t].why); bad += a[t].bad; }
        }
        xv_native_70110_force(0);
        free(a); free(th);
    }
    printf("native-70110 threads: %u scenes, %d threads x %d runs each, mode %d: %u runs, %u mismatches, verify mismatches %u\n",
           scenes, nth, iters, mode, runs, bad, xv_native_70110_mismatch_total());
    return bad != 0 || xv_native_70110_mismatch_total() != 0;
}
#endif

/* ---- --replay <file> [reps]: entry states captured in the game (XV_NATIVE_70110_CAPTURE) ---------------------------
 * The captured page table, and per call the context and every block the body reads (frame window, material, render
 * context and its tables, the global/camera/constant pages, the render-state page). Each call runs the guest body, the
 * native and verify mode from its own state (with this file's callee/HLE stand-ins) and is compared like a random case
 * (the frame window, the render-state page, the context, the events). reps > 0: then the timing of guest and native
 * over all calls, alternating passes (the cheap stand-ins, as --bench). */
typedef struct { uint32_t a, n; uint8_t *b; } rblock;
typedef struct { xctx c; unsigned nb; rblock bl[16]; } rrec;
static void rput(const rrec *q) { for (unsigned i = 0; i < q->nb; ++i) x_guest_write_pages(q->bl[i].a, q->bl[i].b, q->bl[i].n); }
static int replay(const char *path, int reps)
{
    FILE *f = fopen(path, "rb"); if (!f) { perror(path); return 2; }
    uint32_t hdr[3];
    if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x4337304Eu || hdr[1] != (1u << 20) || hdr[2] != sizeof(xctx)) { fprintf(stderr, "bad capture\n"); return 2; }
    g_xpt = malloc(4u << 20);
    if (fread(g_xpt, 4, 1u << 20, f) != (1u << 20)) { fprintf(stderr, "short capture\n"); return 2; }
    uint32_t top = 0; for (uint32_t i = 0; i < (1u << 20); ++i) if (g_xpt[i] > top) top = g_xpt[i];
    g_xram = calloc(1, (size_t)top + 0x2000u); g_img_base = g_xram;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
    unsigned n = 0, cap = 256; rrec *rs = malloc(sizeof *rs * cap);
    for (;;) {
        if (n == cap) { cap *= 2; rs = realloc(rs, sizeof *rs * cap); }
        rrec *q = &rs[n];
        if (fread(&q->c, sizeof q->c, 1, f) != 1 || fread(&q->nb, 4, 1, f) != 1) break;
        unsigned k = 0; int ok = 1;
        for (unsigned i = 0; i < q->nb && ok; ++i) {
            uint32_t an[2]; if (fread(an, 8, 1, f) != 1) { ok = 0; break; }
            if (k < 16) { q->bl[k].a = an[0]; q->bl[k].n = an[1]; q->bl[k].b = malloc(an[1]); if (fread(q->bl[k].b, 1, an[1], f) != an[1]) ok = 0; k++; }
        }
        if (!ok) break;
        q->nb = k; n++;
    }
    fclose(f);
    printf("replay: %u calls\n", n);
    result *rg = malloc(sizeof *rg), *rn = malloc(sizeof *rn), *rv = malloc(sizeof *rv);
    unsigned bad = 0, vbad = 0, native = 0;
    const unsigned W = 0x120u + 0x1000u;
    uint8_t *wg = malloc(W), *wn = malloc(W), *wv = malloc(W);
    for (unsigned i = 0; i < n; ++i) {
        const rrec *q = &rs[i]; const uint32_t lo = q->c.r[4] - 0x100u;
        rput(q); run(0, &q->c, rg); x_guest_read_pages(wg, lo, 0x120); x_guest_read_pages(wg + 0x120, 0x18F000u, 0x1000);
        rput(q); run(2, &q->c, rn); x_guest_read_pages(wn, lo, 0x120); x_guest_read_pages(wn + 0x120, 0x18F000u, 0x1000);
        char why[400] = "";
        /* the comparison of random cases, on the window and the render-state page */
        uint8_t *snapg = calloc(1, SNAP_BYTES), *snapn = calloc(1, SNAP_BYTES);
        memcpy(snapg, wg, W); memcpy(snapn, wn, W);
        if (!same(rg, rn, snapg, snapn, why, sizeof why, 0)) { if (++bad <= 10) printf("call %u native MISMATCH: %s\n", i, why); }
        const unsigned before = xv_native_70110_mismatch_total();
        rput(q); run(1, &q->c, rv); x_guest_read_pages(wv, lo, 0x120); x_guest_read_pages(wv + 0x120, 0x18F000u, 0x1000);
        uint8_t *snapv = calloc(1, SNAP_BYTES); memcpy(snapv, wv, W);
        if (!same(rg, rv, snapg, snapv, why, sizeof why, 1)) { if (++vbad <= 10) printf("call %u verify result MISMATCH: %s\n", i, why); }
        else if (xv_native_70110_mismatch_total() != before) { if (++vbad <= 10) printf("call %u verify reported mismatches\n", i); }
        native += rn->log.n > 0;
        free(snapg); free(snapn); free(snapv);
    }
    printf("replay: %u calls compared, %u mismatches, verify %u, nan words %u\n", n, bad, vbad, nan_words);
    if (reps > 0) {
        light = 1; pe_open();
        double ns[2] = { 0, 0 }; uint64_t pe[2][2] = { { 0, 0 }, { 0, 0 } };
        for (int r = 0; r < reps; ++r)
            for (int mode = 0; mode <= 2; mode += 2) {
                xv_native_70110_force(mode);
                for (unsigned i = 0; i < n; ++i) {
                    rput(&rs[i]); xctx c = rs[i].c; c.preempt = 1 << 30;
                    uint64_t p0[2], p1[2]; struct timespec t0, t1;
                    pe_read(p0); clock_gettime(CLOCK_MONOTONIC, &t0);
                    f_00070110(&c);
                    clock_gettime(CLOCK_MONOTONIC, &t1); pe_read(p1);
                    ns[mode / 2] += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    pe[mode / 2][0] += p1[0] - p0[0]; pe[mode / 2][1] += p1[1] - p0[1];
                }
                xv_native_70110_force(0);
            }
        const double calls = (double)n * reps;
        printf("replay speed: %u calls x %d: guest %.0f ns/call, native %.0f ns/call (%.2fx); user instructions/call guest %.0f native %.0f (%.2fx), "
               "cycles/call guest %.0f native %.0f (%.2fx)%s\n", n, reps, ns[0] / calls, ns[1] / calls, ns[0] / ns[1], pe[0][0] / calls, pe[1][0] / calls,
               pe[1][0] ? (double)pe[0][0] / pe[1][0] : 0, pe[0][1] / calls, pe[1][1] / calls, pe[1][1] ? (double)pe[0][1] / pe[1][1] : 0,
               pe_fd[0] < 0 ? " (perf counters unavailable)" : "");
    }
    log_all = 1; xv_native_70110_report(0);
    return bad || vbad;
}

int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 2000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    const int verify = !(argc > 3 && !strcmp(argv[3], "--no-verify"));
    if (argc > 4 && !strcmp(argv[3], "--replay")) return replay(argv[4], argc > 5 ? atoi(argv[5]) : 0);
    map_memory();
    if (getenv("N70_HLE_TIMING")) xv_hle_timing = atoi(getenv("N70_HLE_TIMING"));   /* the XV_HLE_TIMING branch of every HLE call */
    if (argc > 4 && !strcmp(argv[3], "--threads")) {
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
        return threads_test(cases, atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 20);
#else
        fprintf(stderr, "--threads needs the per-thread page table build (-DXV_THREAD_PAGE_TABLE=1)\n"); return 2;
#endif
    }
    if (argc > 4 && !strcmp(argv[3], "--bench")) {
        const int reps = atoi(argv[4]); game_like = 1; light = 1;
        pe_open(); prof_start();
        double ns[2] = { 0, 0 }; uint64_t pe[2][2] = { { 0, 0 }, { 0, 0 } }; unsigned calls = 0;
        xctx *cs = malloc(sizeof *cs * cases); uint8_t **snaps = malloc(sizeof *snaps * cases);
        for (unsigned k = 0; k < cases; ++k) { scene_t s; scene(&s); init_ctx(&cs[k], &s); snaps[k] = malloc(SNAP_BYTES); snap(snaps[k]); }
        for (int r = 0; r < reps; ++r)
            for (int mode = 0; mode <= 2; mode += 2) {
                xv_native_70110_force(mode);
                for (unsigned k = 0; k < cases; ++k) {
                    unsnap(snaps[k]); xctx c = cs[k];
                    uint64_t p0[2], p1[2]; struct timespec t0, t1;
                    pe_read(p0); clock_gettime(CLOCK_MONOTONIC, &t0);
                    prof_on = mode == 2 || getenv("N70_PROF_GUEST") != NULL;
                    f_00070110(&c);
                    prof_on = 0;
                    clock_gettime(CLOCK_MONOTONIC, &t1); pe_read(p1);
                    ns[mode / 2] += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec);
                    pe[mode / 2][0] += p1[0] - p0[0]; pe[mode / 2][1] += p1[1] - p0[1];
                    calls += mode == 0;
                }
                xv_native_70110_force(0);
            }
        prof_stop();
        /* the stand-ins alone: the 19 callees and 40 HLE calls of a full call, on a copy of the context (both sides pay
         * them; subtract for the body's own cost) */
        double sns = 0; uint64_t spe[2] = { 0, 0 };
        static const uint32_t ids[19] = { 0x80360u, 0x80360u, 0x80360u, 0x80360u, 0xB5130u, 0x173F20u, 0x111A0u, 0x658D0u, 0x56F20u,
                                          0x118D0u, 0x11B60u, 0x11BD0u, 0x11BD0u, 0x11BD0u, 0x11BD0u, 0x6F340u, 0x7A960u, 0x56F20u, 0x7A960u };
        for (int r = 0; r < reps; ++r)
            for (unsigned k = 0; k < cases; ++k) {
                xctx c = cs[k]; unsnap(snaps[k]);
                uint64_t p0[2], p1[2]; struct timespec t0, t1;
                pe_read(p0); clock_gettime(CLOCK_MONOTONIC, &t0);
                for (int i = 0; i < 19; ++i) { c.r[4] -= 8; callee(&c, ids[i], 4, i == 4 || i == 5 || i == 9, 0, 0); }
                for (int i = 0; i < 40; ++i) { c.r[4] -= 8; hle(&c, 1 + (unsigned)i % 6, 1); }
                clock_gettime(CLOCK_MONOTONIC, &t1); pe_read(p1);
                sns += (t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec); spe[0] += p1[0] - p0[0]; spe[1] += p1[1] - p0[1];
            }
        const double n = (double)calls;
        printf("bench: the stand-ins alone (19 callees + 40 HLE calls): %.0f ns, %.0f instructions, %.0f cycles per call\n", sns / n, spe[0] / n, spe[1] / n);
        printf("bench: %u scenes x %d: guest %.0f ns/call, native %.0f ns/call (%.2fx); user instructions/call guest %.0f native %.0f (%.2fx), "
               "cycles/call guest %.0f native %.0f (%.2fx)%s\n", cases, reps, ns[0] / n, ns[1] / n, ns[0] / ns[1],
               pe[0][0] / n, pe[1][0] / n, pe[1][0] ? (double)pe[0][0] / pe[1][0] : 0, pe[0][1] / n, pe[1][1] / n,
               pe[1][1] ? (double)pe[0][1] / pe[1][1] : 0, pe_fd[0] < 0 ? " (perf counters unavailable)" : "");
        log_all = 1; xv_native_70110_report(0);
        return 0;
    }
    uint8_t *m0 = malloc(SNAP_BYTES), *mg = malloc(SNAP_BYTES), *mn = malloc(SNAP_BYTES), *mv = malloc(SNAP_BYTES);
    result *rg = malloc(sizeof *rg), *rn = malloc(sizeof *rn), *rv = malloc(sizeof *rv);
    unsigned bad = 0, vbad = 0, declined_guest = 0, events = 0, hle = 0, callees = 0, straddle = 0;
    for (unsigned k = 0; k < cases; ++k) {
        scene_t s; scene(&s); xctx c0; init_ctx(&c0, &s);
        straddle += ((s.E - 0x100u) >> 12) != ((s.E + 0x1Fu) >> 12);
        snap(m0);
        run(0, &c0, rg); snap(mg);
        unsnap(m0); run(2, &c0, rn); snap(mn);
        events += rg->log.n;
        for (unsigned i = 0; i < rg->log.n && i < EV_MAX; ++i) { hle += rg->log.e[i].kind == 2; callees += rg->log.e[i].kind == 1; }
        char why[400] = "";
        if (!same(rg, rn, mg, mn, why, sizeof why, 0)) { if (++bad <= 10) printf("case %u native MISMATCH: %s\n", k, why); }
        if (verify) {
            const unsigned before = xv_native_70110_mismatch_total();
            unsnap(m0); run(1, &c0, rv); snap(mv);
            if (!same(rg, rv, mg, mv, why, sizeof why, 1)) { if (++vbad <= 10) printf("case %u verify result MISMATCH: %s\n", k, why); }
            else if (xv_native_70110_mismatch_total() != before) { if (++vbad <= 10) printf("case %u verify reported %u mismatches\n", k, xv_native_70110_mismatch_total() - before); }
        }
        unsnap(m0);
        (void)declined_guest;
    }
    log_all = 1; xv_native_70110_report(0); log_all = 0;
    printf("native-70110: %u cases (%u with the window across a page end), %u mismatches, verify %u; %u events (%u callee calls, %u HLE calls), "
           "hook-args %u, nan words %u\n", cases, straddle, bad, vbad, events, callees, hle, hook_args_bad, nan_words);
    return bad || vbad || hook_args_bad;
}
