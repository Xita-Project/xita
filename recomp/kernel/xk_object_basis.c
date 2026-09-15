/* Opt-in Halo 3925 object basis preparation, 0x8E166..0x8E292.
 * Keep both matrices and the lifted x87/flag effects. The guest owner invokes
 * this synchronously; there is no worker or cross-frame object cache. */
#ifdef XV_NATIVE_OBJECT_BASIS
#include "xk.h"
#include "xk_object_jobs.h"
#include <stdlib.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif

static unsigned basis_used, basis_mirrored, basis_declined[3];
/* Guest-owner benchmark override; -1 restores the configured default. */
static int basis_override = -1;
void xv_object_basis_override(int value)
{
    basis_override = value < 0 ? -1 : value != 0;
}
static int enabled(void)
{
    static int value = -1, math_allowed;
    if (value < 0) {
        const char *basis = getenv("XV_NATIVE_OBJECT_BASIS");
        const char *math = getenv("XV_NATIVE_MATH");
        value = basis && atoi(basis) != 0;
        math_allowed = !math || atoi(math) != 0;
    }
    return math_allowed && (basis_override < 0 ? value : basis_override);
}
static int fp_supported(void)
{
#if defined(__arm__)
    uint32_t value;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(value) :: "memory");
    return !(value & 0x00009F00u);
#elif defined(__x86_64__)
    return (_mm_getcsr() & 0x1F80u) == 0x1F80u;
#else
    return 0;
#endif
}
static int decline(unsigned reason) { basis_declined[reason]++; return 0; }
static void *span(uint32_t address, unsigned size)
{
    if ((address & 3u) || (uint64_t)address + size > 0x100000000ull) return NULL;
    uintptr_t first = (uintptr_t)X_G(address);
    for (uint64_t page = ((uint64_t)address & ~4095ull) + 4096;
         page < (uint64_t)address + size; page += 4096)
        if ((uintptr_t)X_G((uint32_t)page) != first + (page - address)) return NULL;
    return (void *)first;
}
static double load_float(uint32_t word)
{
    float value;
    memcpy(&value, &word, 4);
#if defined(__arm__)
    double result;
    __asm__("vcvt.f64.f32 %P0, %1" : "=w"(result) : "w"(value));
    return result;
#else
    return (double)value;
#endif
}
static uint32_t store_float(double value)
{
    float rounded;
#if defined(__arm__)
    __asm__("vcvt.f32.f64 %0, %P1" : "=w"(rounded) : "w"(value));
#else
    rounded = (float)value;
#endif
    uint32_t word;
    memcpy(&word, &rounded, 4);
    return word;
}
/* GCC can select VNMLS even with FP contraction disabled, which changes NaN
 * signs. Explicit scalar VFP operations retain the original ARM operand order
 * and the intermediate conversions, including FZ/DN behavior when mirrored. */
static double mul(double a, double b)
{
#if defined(__arm__)
    double result;
    __asm__("vmul.f64 %P0, %P1, %P2" : "=w"(result) : "w"(a), "w"(b));
    return result;
#else
    return a*b;
#endif
}
static double sub(double a, double b)
{
#if defined(__arm__)
    double result;
    __asm__("vsub.f64 %P0, %P1, %P2" : "=w"(result) : "w"(a), "w"(b));
    return result;
#else
    return a-b;
#endif
}
static double negate(double value)
{
#if defined(__arm__)
    double result;
    __asm__("vneg.f64 %P0, %P1" : "=w"(result) : "w"(value));
    return result;
#else
    return -value;
#endif
}

int xv_math_object_basis(xctx *c)
{
    XV_OBJECT_MATH_GUARD();
    if (!enabled()) return decline(0);
    /* Reordering guest stores around native math is safe only with masked
     * native FP exceptions. Unsupported controls use the original region. */
    if (!fp_supported()) return decline(1);
    uint32_t object = c->r[5], sp = c->r[4];
    if (object > UINT32_MAX - 0x3bu || sp > UINT32_MAX - 0xa7u) return decline(2);
    const void *input = span(object + 4u, 56);
    void *output = span(sp + 0x40u, 104);
    if (!input || !output) return decline(2);
    uintptr_t a = (uintptr_t)input, b = (uintptr_t)output;
    if (a < b + 104u && b < a + 56u) return decline(2);
    const uint32_t *in = input;
    uint32_t *out = output;
    /* These six products follow the original double x87 emulation order;
     * each cross component rounds to float before optional mirroring. */
    double x = sub(mul(load_float(in[10]), load_float(in[12])),
                   mul(load_float(in[9]), load_float(in[13])));
    double y = sub(mul(load_float(in[8]), load_float(in[13])),
                   mul(load_float(in[10]), load_float(in[11])));
    double second = mul(load_float(in[12]), load_float(in[8]));
    double z = sub(mul(load_float(in[11]), load_float(in[9])), second);
    memset(out, 0, 104);
    out[0] = out[13] = out[14] = out[18] = out[22] = 0x3f800000u;
    memcpy(out + 1, in + 8, 12);
    memcpy(out + 7, in + 11, 12);
    memcpy(out + 23, in + 2, 12);
    out[4] = store_float(x); out[5] = store_float(y); out[6] = store_float(z);
    if (in[0] & 0x1000u) {
        out[4] = store_float(negate(load_float(out[4])));
        out[5] = store_float(negate(load_float(out[5])));
        z = negate(load_float(out[6]));
        out[6] = store_float(z);
        basis_mirrored++;
    }
    c->st[(c->fsp - 1u) & 7u] = z;
    c->st[(c->fsp - 2u) & 7u] = second;
    c->r[0] = in[0]; c->r[1] = in[13]; c->r[2] = in[11];
    X_FLAGS(XK_LOGIC, 0, 0, (in[0] >> 8) & 0x10u, 8);
    basis_used++;
    return 1;
}

void xv_object_basis_report(unsigned frames)
{
    XV_OBJECT_MATH_GUARD();
    XK_LOG("[object-basis] %u frames used %u mirrored %u declined disabled %u fp %u layout %u\n",
           frames,basis_used,basis_mirrored,basis_declined[0],basis_declined[1],basis_declined[2]);
    basis_used = basis_mirrored = 0;
    memset(basis_declined, 0, sizeof basis_declined);
}
#endif
