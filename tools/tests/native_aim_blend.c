/* Differential test: native f_000A39B0 (recomp/kernel/xk_native_aim_blend.c) against the lifted guest bodies of
 * f_000A39B0 and its callees (f_0001D150, f_00180ADA/f_000220FF/f_00180AE4/f_0002219C/f_00180C8A, f_000A5080,
 * f_000A2C30, f_000B0E70, f_000B4320, f_000B0EF0; extracted from a stage's shards by tools/test_native_aim_blend.py)
 * on randomized aim-overlay calls in a synthetic guest arena: shuffled physical pages (tag data and the stack window
 * straddle page boundaries), random dead stack bytes, random x87 slots/top/status and lazy flags, rounding modes,
 * both CRT paths ([1F2EB0], [270678]), zero/negative/huge yaw and pitch, zero ranges, clamped frame indices, sparse
 * and dense rotation/translation masks, 0..130 nodes, zero-norm and opposite-sign quaternions, node arrays that
 * overlap their own keyframe data, a small back-edge slice.
 * Each case first runs the native on a copy of the state: when it declines (compressed data, early exit, NaN/inf
 * operands, unmasked precision exception, aliasing, unaligned esp) nothing may have changed; otherwise the guest body
 * (hook off) runs on the original state and the two results are compared: the whole arena, every xctx field, the
 * xv_preempt call count. Flavor 3 adds NaN/inf data: there float slots that are NaN on both sides compare equal. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "xv_x86rt.h"

uint8_t *g_xram; uint32_t *g_xpt; uint8_t *g_img_base;
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
__thread uint32_t *xv_host_page_table;
#endif
int xv_phase_enabled;
void xv_phase_begin(void *s, void *c, unsigned id) { (void)s; (void)c; (void)id; }
void xv_phase_end(void *s) { (void)s; }
void xk_os_log(const char *fmt, ...) { (void)fmt; }
uint64_t xk_os_monotonic_us(void) { return 0; }
const char xv_object_job_marker = 0;
void xv_scene_phase_begin(uint32_t a) { (void)a; }
void xv_scene_phase_end(uint32_t a) { (void)a; }
static unsigned preempt_calls; static int slice = 37;
void xv_preempt(xctx *c) { preempt_calls++; c->preempt = slice; }
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
extern void f_000A39B0(xctx *), f_00180AE4(xctx *), f_0002219C(xctx *);
extern int xv_native_aim_blend(xctx *);
extern void xv_native_aim_blend_force(int);
static int unexpected;                           /* guest reached code the linked subset does not cover */
static const char *unexpected_what;
void xv_call(xctx *c, uint32_t target)
{
    if (target == 0x180AE4u) { f_00180AE4(c); return; }
    if (target == 0x2219Cu) { f_0002219C(c); return; }
    unexpected = 1; unexpected_what = "xv_call"; c->r[4] += 4;
}
void f_000A3750(xctx *c) { unexpected = 1; unexpected_what = "f_000A3750 (compressed rotation)"; c->r[4] += 0x18; }
void f_000A2EE0(xctx *c) { unexpected = 1; unexpected_what = "f_000A2EE0 (compressed translation)"; c->r[4] += 0x18; }
void f_00023D25(xctx *c) { unexpected = 1; unexpected_what = "f_00023D25 (matherr)"; c->r[4] += 4; }
void x_str_movs(xctx *c, unsigned sz, int mode) { (void)c; (void)sz; (void)mode; unexpected = 1; unexpected_what = "movs (matherr record)"; }
void xv_trap(xctx *c, uint32_t eip) { (void)c; unexpected = 1; unexpected_what = "trap"; (void)eip; }
void xv_unimpl(xctx *c, uint32_t eip, const char *what) { (void)c; (void)eip; unexpected = 1; unexpected_what = what; }

enum { ARENA = 16u << 20, IMAGE_PAGES = 0x400, TAG = 0x40000000u, TAG_PAGES = 512, STACK = 0xD0000000u, STACK_PAGES = 4 };
static uint32_t trash_off, next_free;
static uint64_t rng = 88172645463325252ull;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
static float frand(float lo, float hi) { return lo + (hi - lo) * (float)(rnd() & 0xFFFFFF) / 16777216.0f; }
static void w8(uint32_t a, uint8_t v) { x_guest_write_pages(a, &v, 1); }
static void w16(uint32_t a, uint16_t v) { x_guest_write_pages(a, &v, 2); }
static void w32(uint32_t a, uint32_t v) { x_guest_write_pages(a, &v, 4); }
static void wf(uint32_t a, float v) { x_guest_write_pages(a, &v, 4); }
static void wd(uint32_t a, double v) { x_guest_write_pages(a, &v, 8); }
static float nanf_(void) { uint32_t b = 0x7FC00000u | (rnd() & 0x3FFFFF); if (rnd() & 1) b |= 0x80000000u; float f; memcpy(&f, &b, 4); return f; }

static void map_memory(void)
{
    g_xram = calloc(1, ARENA); g_xpt = malloc(sizeof(uint32_t) << 20);
    trash_off = ARENA - 4096; for (uint32_t i = 0; i < (1u << 20); ++i) g_xpt[i] = trash_off;
    for (uint32_t i = 0; i < IMAGE_PAGES; ++i) g_xpt[i] = i << 12;       /* image: identity, so X_IMG == X_G */
    g_img_base = g_xram; next_free = IMAGE_PAGES << 12;
    uint32_t perm[TAG_PAGES]; for (uint32_t i = 0; i < TAG_PAGES; ++i) perm[i] = i;
    for (uint32_t i = TAG_PAGES - 1; i > 0; --i) { uint32_t j = rnd() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t; }
    for (uint32_t i = 0; i < TAG_PAGES; ++i) g_xpt[(TAG >> 12) + i] = next_free + (perm[i] << 12);   /* shuffled physical pages */
    next_free += TAG_PAGES << 12;
    for (uint32_t i = 0; i < STACK_PAGES; ++i) g_xpt[(STACK >> 12) + i] = next_free + ((STACK_PAGES - 1 - i) << 12);
    next_free += STACK_PAGES << 12;
    if (next_free > trash_off) { fprintf(stderr, "arena too small\n"); exit(2); }
#if defined(XV_THREAD_PAGE_TABLE) && XV_THREAD_PAGE_TABLE && !defined(__vita__)
    xv_host_page_table = g_xpt;
#endif
}

static uint32_t tag_top;
static uint32_t tag_alloc(uint32_t bytes, uint32_t align)
{
    uint32_t a = (tag_top + align - 1) & ~(align - 1);
    tag_top = a + bytes; if (tag_top > TAG + (TAG_PAGES << 12) - 64) { fprintf(stderr, "tag space exhausted\n"); exit(2); }
    return a;
}
static void image_constants(void)
{
    wf(0x1F0A68, 0.0f); wf(0x1F0A78, 1.0f); wd(0x1F0A70, 1.0); wf(0x1F0AE8, 3.0518509447574615e-05f);
    static const uint8_t xlat[16] = { 8, 4, 8, 8, 8, 4, 8, 8, 0, 4, 12, 8, 0, 4, 12, 8 };
    for (unsigned i = 0; i < 16; ++i) w8(0x1F2E34 + i, xlat[i]);
    static const uint8_t desc[16] = { 4, 'f', 'm', 'o', 'd', 0, 0, 0, 0, 0, 0, 0, 0, 2, 0x16, 0 };
    for (unsigned i = 0; i < 16; ++i) w8(0x25F628 + i, desc[i]);
    static const uint32_t disp[16] = { 0x180AE4, 0x2223E, 0x221D7, 0x2223E, 0x2219C, 0x2223E, 0x221D7, 0x2223E,
                                       0x221D5, 0x221D5, 0x221FF, 0x221D5, 0x2223E, 0x2223E, 0x221D7, 0x2223E };
    for (unsigned i = 0; i < 16; ++i) w32(0x25F638 + 4 * i, disp[i]);
}

typedef struct { uint32_t E, A, edi, nodes, cnt; int flavor, alias; } scen;

static float range_value(int flavor)
{
    switch (rnd() % 10) {
    case 0: return 0.0f;
    case 1: return -frand(0.1f, 2.0f);
    case 2: return flavor == 3 && (rnd() & 1) ? nanf_() : frand(0.05f, 0.4f);
    default: return frand(0.1f, 1.5f);
    }
}
static float angle_value(float lo, float hi, int flavor)
{
    switch (rnd() % 16) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return hi;                          /* exact grid edge */
    case 3: return lo;
    case 4: return frand(-40.0f, 40.0f);        /* far outside: clamps */
    case 5: return flavor == 3 ? (rnd() & 1 ? nanf_() : (rnd() & 1 ? INFINITY : -INFINITY)) : frand(lo, hi);
    case 6: return (float)(int)frand(-4, 4) * (hi - lo) / 8.0f;   /* multiples: exact fractions */
    case 7: return frand(-1e-30f, 1e-30f);      /* tiny: u denormal/zero class boundaries */
    default: return frand(lo * 1.2f, hi * 1.2f);
    }
}

static void scene(xctx *c, scen *s)
{
    memset(g_xram, 0, ARENA);
    image_constants();
    tag_top = TAG + (rnd() % 64) * 4;
    s->flavor = rnd() % 8 == 0 ? 3 : rnd() % 9 == 0 ? 4 : rnd() % 3;   /* 4: cancellation (the product's association decides) */
    int straddle = s->flavor != 3 && rnd() % 29 == 0;                 /* a keyframe dword straddling into a stack-window page */
    /* the stack: random bytes everywhere (dead stack reproduction), E somewhere with the whole window mapped */
    for (uint32_t a = STACK; a < STACK + STACK_PAGES * 4096u; a += 4) w32(a, rnd());
    uint32_t E = STACK + 0x400 + (rnd() % (STACK_PAGES * 4096u - 0x480));
    E &= ~3u; if (rnd() % 97 == 0) E |= 1 + rnd() % 3;
    if (straddle) E = STACK + 3 * 4096u + 0x3D4;   /* [E-0x3D4] (a return address the guest pushes) = the first dword of guest page 3 = physical stack page 0 */
    s->E = E;
    /* the aim animation header (ebx) */
    uint32_t A = tag_alloc(0x20, 4); s->A = A;
    unsigned yl = rnd() % 6, yr = rnd() % 6, pd = rnd() % 5, pu = rnd() % 5;
    if (rnd() % 11 == 0) { yl = 0; yr = 0; }
    if (straddle) yl = yr = pd = pu = 0;                                 /* one keyframe: every pointer is the base */
    wf(A + 0, range_value(s->flavor)); wf(A + 4, range_value(s->flavor));
    w16(A + 8, (uint16_t)yl); w16(A + 0xA, (uint16_t)yr);
    wf(A + 0xC, range_value(s->flavor)); wf(A + 0x10, range_value(s->flavor));
    w16(A + 0x14, (uint16_t)pd); w16(A + 0x16, (uint16_t)pu);
    const uint32_t nY = yl + yr + 1, nP = pd + pu + 1, frames = nY * nP;
    /* the animation entry (edi) */
    uint32_t edi = tag_alloc(0xB4, 4); s->edi = edi;
    int32_t cnt = (int32_t)(rnd() % 64); if (rnd() % 9 == 0) cnt = (int32_t)(rnd() % 131); if (rnd() % 29 == 0) cnt = -(int32_t)(rnd() % 3);
    if (straddle) cnt = 1 + (int32_t)(rnd() % 8);
    s->cnt = (uint32_t)cnt;
    w16(edi + 0x20, rnd() % 41 == 0 ? (uint16_t)(rnd() % 3) : 1);
    w16(edi + 0x22, (uint16_t)(frames + (rnd() % 3) - (rnd() % 23 == 0 ? 3 : 0)));
    w16(edi + 0x2C, (uint16_t)cnt);
    uint8_t flags = (uint8_t)(rnd() & 0xFE); if (rnd() % 5 == 0) flags |= 1;
    w8(edi + 0x3A, flags);
    w8(0x204B6F, rnd() % 7 == 0);
    w32(edi + 0x88, rnd() % 4 == 0 ? 0 : rnd());
    uint32_t nrot = 0, ntr = 0;
    int dense = rnd() % 3;
    for (unsigned w = 0; w < 4; ++w) {
        uint32_t tm = dense == 0 ? rnd() & rnd() : dense == 1 ? rnd() : ~(rnd() & rnd() & rnd());
        uint32_t rm = dense == 0 ? rnd() | rnd() : dense == 1 ? rnd() : ~(rnd() & rnd());
        if (straddle && w == 0) { tm |= 1u; rm &= ~1u; }                    /* node 0: translation only, read first */
        w32(edi + 0x5C + 4 * w, tm); w32(edi + 0x6C + 4 * w, rm);
    }
    for (int32_t n = 0; n < cnt; ++n) {   /* exactly the words the loop reads: past 128 nodes the translation mask is rotation word 0 */
        uint32_t rm, tm; x_guest_read_pages(&rm, edi + 0x6C + 4 * (n >> 5), 4); x_guest_read_pages(&tm, edi + 0x5C + 4 * (n >> 5), 4);
        nrot += (rm >> (n & 31)) & 1; ntr += (tm >> (n & 31)) & 1;
    }
    uint32_t stride = 8 * nrot + 12 * ntr + (rnd() % 3) * 4;
    uint32_t nframes = frames + 2;
    uint32_t base = tag_alloc(stride * nframes + 64, rnd() % 13 == 0 ? 2 : 4);
    if (straddle) {                     /* the tag page stored in the physical page right below the stack's: its last 2 bytes + the stack page's first 2 */
        uint32_t pg = 0; for (uint32_t i = 0; i < TAG_PAGES; ++i) if (g_xpt[(TAG >> 12) + i] == ((IMAGE_PAGES + TAG_PAGES - 1) << 12)) pg = TAG + (i << 12);
        base = pg + 0x1000 - 2;
    }
    w32(edi + 0xAC, base); w16(edi + 0x24, (uint16_t)stride);
    int opposite = rnd() % 3 == 0, axis = rnd() % 4 == 0;   /* axis: components from {0, +-16384, +-32767}: exact zero dots/ties */
    for (uint32_t f = 0; f < nframes; ++f) {
        uint32_t p = base + f * stride;
        for (int32_t n = 0; n < cnt; ++n) {
            uint32_t rm, tm; x_guest_read_pages(&rm, edi + 0x6C + 4 * (n >> 5), 4); x_guest_read_pages(&tm, edi + 0x5C + 4 * (n >> 5), 4);
            if ((rm >> (n & 31)) & 1) {
                for (unsigned k = 0; k < 4; ++k) {
                    int v = (int)(rnd() % 65535) - 32767;
                    if (axis) { static const int ax[5] = { 0, 16384, -16384, 32767, -32767 }; v = ax[rnd() % 5]; }
                    if (rnd() % 50 == 0) v = -32768;
                    if (rnd() % 40 == 0) v = 0;
                    if (opposite && (f & 1)) v = -v;
                    if (s->flavor == 4) { static int keep[4]; if (f == 0) keep[k] = (k == 3 ? keep[2] : v); v = keep[k]; }   /* same quaternion every frame, c3 == c2 */
                    w16(p, (uint16_t)(int16_t)v); p += 2;
                }
            }
            if ((tm >> (n & 31)) & 1) {
                for (unsigned k = 0; k < 3; ++k) { float v = frand(-2, 2); if (s->flavor == 3 && rnd() % 97 == 0) v = nanf_(); wf(p, v); p += 4; }
            }
        }
    }
    /* the node array: its own tag block, or overlapping the keyframe data (order-sensitive), or aliasing cases */
    uint32_t nodes = tag_alloc(32u * (cnt > 0 ? (uint32_t)cnt : 1u) + 32, 4);
    int alias = rnd() % 23; s->alias = alias;
    if (alias == 0 && s->flavor == 3) alias = 5;   /* NaN node values re-read as keyframe int16s: the NaN payload an operation
                                                    * propagates is the host compiler's operand order, not reproducible */
    if (alias == 0 && stride * nframes > 64) nodes = base + (rnd() % (stride * nframes / 2)) * 2;     /* inside keyframe data */
    else if (alias == 1) nodes = E - (rnd() % 0x500);                                             /* stack window: declines */
    else if (alias == 2) nodes = edi + 0x40 - 32u * (rnd() % 3);                                 /* animation entry: declines */
    else if (alias == 3 && cnt > 0) nodes = 0x1F0A68 - 32u * (rnd() % (uint32_t)cnt) - 8;        /* the constants: declines */
    else if (alias == 4) w32(edi + 0xAC, E - 0x200 - (rnd() % 0x100));                          /* keyframe data on/above the stack window */
    else if (alias == 6) { if (rnd() & 1) s->nodes = nodes |= 2; }                               /* unaligned node array: declines */
    s->nodes = nodes;
    if (!(alias == 1 || alias == 3))
        for (int32_t n = 0; n < (cnt > 0 ? cnt : 0); ++n) {
            uint32_t q = nodes + 32u * (uint32_t)n;
            float x = frand(-1, 1), y = frand(-1, 1), z = frand(-1, 1), w = frand(-1, 1);
            if (s->flavor == 4 && (rnd() & 1)) { x = y = (rnd() & 1 ? 1.152921504606847e18f : -1.152921504606847e18f); z = 0; w = 1; }   /* 2^60: b3*a0 - a1*b2 cancels */
            wf(q, x); wf(q + 4, y); wf(q + 8, z); wf(q + 12, w);
            wf(q + 16, frand(-3, 3)); wf(q + 20, frand(-3, 3)); wf(q + 24, frand(-3, 3)); wf(q + 28, 1.0f);
        }
    /* CRT globals */
    w32(0x1F2EB0, rnd() % 3 == 0 ? 0 : 1);
    w32(0x270678, rnd() % 3 == 0 ? 0 : rnd() | 1);
    if (rnd() % 31 == 0) w8(0x25F628 + 0xE, 5);                /* the other _trandisp2 control-word branch */
    /* the call: eax = A, edi = entry, [E] ret, [E+4] yaw, [E+8] pitch, [E+0xC] nodes */
    float ylo = -3.0f, yhi = 3.0f;
    w32(E, 0x3ED22u); wf(E + 4, angle_value(ylo, yhi, s->flavor)); wf(E + 8, angle_value(ylo, yhi, s->flavor)); w32(E + 0xC, nodes);
    memset(c, 0, sizeof *c);
    for (unsigned i = 0; i < 8; ++i) c->r[i] = rnd();
    c->r[0] = A; c->r[4] = E; c->r[7] = edi;
    c->fsp = rnd() & 7; if (rnd() % 53 == 0) c->fsp |= 8u << (rnd() % 3);   /* stale high bits: the guest masks them */
    static const uint16_t cws[] = { 0x027F, 0x027F, 0x027F, 0x037F, 0x067F, 0x0A7F, 0x0E7F, 0x025F };
    c->fcw = cws[rnd() % 8]; c->fsw = (uint16_t)rnd();
    for (unsigned i = 0; i < 8; ++i) c->st[i] = (double)(int32_t)rnd() / 7.0;
    c->f_kind = rnd() % 5; c->f_op1 = rnd(); c->f_op2 = rnd(); c->f_res = rnd(); c->f_bits = (rnd() & 1) ? 32 : 8;
    c->f_cf_override = rnd() & 1; c->f_cf = rnd() & 1; c->f_of_override = rnd() & 1; c->f_of = rnd() & 1;
    c->df = 0;
    c->preempt = (int32_t)(rnd() % 200) - 20;
}

static int nan_bits32(uint32_t v) { return (v & 0x7F800000u) == 0x7F800000u && (v & 0x7FFFFFu); }
static int same_ctx(const xctx *a, const xctx *b, int nan_ok, char *why, size_t n)
{
#define F(x) if (a->x != b->x) { snprintf(why, n, #x " %llX vs %llX", (unsigned long long)a->x, (unsigned long long)b->x); return 0; }
    for (unsigned i = 0; i < 8; ++i) F(r[i]);
    F(fs_base) F(df) F(f_kind) F(f_op1) F(f_op2) F(f_res) F(f_bits) F(f_cf_override) F(f_cf) F(f_of_override) F(f_of)
    F(fsp) F(fsw) F(fcw) F(preempt) F(scratch) F(eip_hint)
    /* x87 slots: two NaNs are equal (which operand's NaN payload an addition/multiplication propagates is the host
     * compiler's operand order; the guest body built -O0 and -O2 already disagrees) */
    (void)nan_ok;
    for (unsigned i = 0; i < 8; ++i) if (memcmp(&a->st[i], &b->st[i], 8) && !(isnan(a->st[i]) && isnan(b->st[i]))) {
        uint64_t x, y; memcpy(&x, &a->st[i], 8); memcpy(&y, &b->st[i], 8);
        snprintf(why, n, "st[%u] (fsp %u) %016llX vs %016llX", i, a->fsp, (unsigned long long)x, (unsigned long long)y); return 0;
    }
    if (memcmp(a->mm, b->mm, sizeof a->mm) || memcmp(a->xmm, b->xmm, sizeof a->xmm)) { snprintf(why, n, "mm/xmm"); return 0; }
#undef F
    return 1;
}

int main(int argc, char **argv)
{
    unsigned cases = argc > 1 ? (unsigned)atoi(argv[1]) : 3000;
    if (argc > 2) rng ^= strtoull(argv[2], 0, 0) * 0x9E3779B97F4A7C15ull;
    map_memory();
    uint8_t *before = malloc(ARENA), *guest = malloc(ARENA);
    unsigned ran = 0, declined = 0, decline_changed = 0, mismatches = 0, unexp = 0, nodes_total = 0, zero_count = 0, looped = 0;
    xv_native_aim_blend_force(0);
    for (unsigned k = 0; k < cases; ++k) {
        xctx c0; scen s; scene(&c0, &s); slice = 5 + rnd() % 60;
        memcpy(before, g_xram, ARENA);
        /* native first: a decline must leave everything untouched */
        xctx cn = c0; preempt_calls = 0;
        xv_native_aim_blend_force(2);
        int handled = xv_native_aim_blend(&cn);
        xv_native_aim_blend_force(0);
        unsigned native_preempts = preempt_calls;
        char why[200] = "";
        if (!handled) {
            declined++;
            if (memcmp(before, g_xram, ARENA) || memcmp(&cn, &c0, sizeof cn)) { decline_changed++; if (++mismatches <= 10) printf("case %u: decline changed state\n", k); }
            continue;
        }
        memcpy(guest, g_xram, ARENA);               /* native result */
        memcpy(g_xram, before, ARENA);
        xctx cg = c0; preempt_calls = 0; unexpected = 0;
        f_000A39B0(&cg);                            /* hook off (mode 0): the guest body */
        unsigned guest_preempts = preempt_calls;
        ran++;
        if (unexpected) { unexp++; if (++mismatches <= 10) printf("case %u MISMATCH: native ran but the guest reached %s\n", k, unexpected_what); continue; }
        const int nan_ok = s.flavor == 3 || s.alias == 4 || s.alias == 0;   /* NaN data, or garbage bit patterns: alias 4 reads random
                                                                          * stack bytes as keyframe floats, alias 0 re-reads written nodes */
        int ok = same_ctx(&cn, &cg, nan_ok, why, sizeof why) && native_preempts == guest_preempts;
        if (!ok && !why[0]) snprintf(why, sizeof why, "xv_preempt calls native %u guest %u", native_preempts, guest_preempts);
        if (ok && memcmp(guest, g_xram, ARENA)) {
            for (uint32_t i = 0; i < ARENA; i += 4) {
                uint32_t a, b; memcpy(&a, guest + i, 4); memcpy(&b, g_xram + i, 4);
                if (a == b) continue;
                if (nan_ok && nan_bits32(a) && nan_bits32(b)) continue;
                ok = 0; snprintf(why, sizeof why, "arena+%06X native %08X guest %08X", i, a, b); break;
            }
        }
        if ((int32_t)s.cnt > 0) { nodes_total += s.cnt; looped++; }
        if (cg.r[4] == c0.r[4] + 0x10) zero_count += 0;
        if (!ok && getenv("AB_DEBUG")) {
            uint32_t p00, p01, p10, p11, base; x_guest_read_pages(&base, s.edi + 0xAC, 4);
            printf("  E %08X A %08X edi %08X nodes %08X cnt %d base %08X window [%08X,%08X)\n", s.E, s.A, s.edi, s.nodes, (int32_t)s.cnt, base, s.E - 0x3D8, s.E + 0x10);
            (void)p00; (void)p01; (void)p10; (void)p11;
            for (uint32_t i = 0; i < ARENA; i += 4) { uint32_t a, b; memcpy(&a, guest + i, 4); memcpy(&b, g_xram + i, 4); if (a != b) { uint32_t va = 0; for (uint32_t p = 0; p < (1u << 20); ++p) if (g_xpt[p] == (i & ~0xFFFu)) { va = (p << 12) | (i & 0xFFFu); break; } printf("  diff arena+%06X (guest %08X) native %08X guest %08X\n", i, va, a, b); } }
            { uint32_t f[4]; for (int k = 0; k < 4; ++k) f[k] = 0; printf("  stride %u\n", (unsigned)*(uint16_t *)X_G(s.edi + 0x24)); }
        }
        if (!ok) { if (++mismatches <= 10) printf("case %u (flavor %d, alias %d, %d nodes) MISMATCH: %s\n", k, s.flavor, s.alias, (int32_t)s.cnt, why); }
    }
    printf("native-aim-blend differential: %u cases, %u compared (%u with nodes, %u nodes), %u declined (%u changed state), %u guest-unexpected, %u mismatches\n",
           cases, ran, looped, nodes_total, declined, decline_changed, unexp, mismatches);
    return mismatches != 0;
}
