/* xk_crt_float.c - native replacements for the game's C-runtime float helpers, which the host scene profile put at
 * ~15% of the guest scene time because MSVC wraps every conversion in _controlfp calls:
 *   0: f_0001EC1F  _controlfp(new, mask): cw = (old & ~mask) | (new & mask); returns the old cw (sign-extended 16)
 *   1: f_0001EABA  _frnd(double): pushes the value rounded per the current control word
 *   2: f_00019E7B  floor/ceil-style: rounds under the control word held at 0x1F2840, restores the cw; finite
 *                  inputs only (NaN/inf and the unmasked-inexact path stay on the guest code)
 * Exact by construction: the same x87_round the emitted code uses, the same register results the callers can see,
 * the same control-word updates. Dead stack slots the guest code wrote below esp are not written.
 * XV_NATIVE_CRT_FLOAT build flag; env XV_NATIVE_CRT_FLOAT (default XV_NATIVE_CRT_FLOAT_DEFAULT) is the kill switch. */
#include "xk.h"
#include "../xv_x86rt.h"
#include <stdlib.h>
#ifndef XV_NATIVE_CRT_FLOAT_DEFAULT
#define XV_NATIVE_CRT_FLOAT_DEFAULT 1
#endif
static unsigned crt_calls[4];
int xv_native_crt_float(xctx *c, unsigned which)
{
    static int on = -1; if (on < 0) { const char *e = getenv("XV_NATIVE_CRT_FLOAT"); on = e ? atoi(e) != 0 : XV_NATIVE_CRT_FLOAT_DEFAULT; XK_LOG("[crt-float] native _controlfp/_frnd/floor %s\n", on ? "enabled" : "disabled"); }
    if (!on) return 0;
    uint32_t esp = c->r[4];
    switch (which) {
    case 0: {
        uint32_t nw = X_M32(esp + 4u), mask = X_M32(esp + 8u); uint16_t old = c->fcw;
        c->r[1] = nw & mask;
        c->fcw = (uint16_t)((old & ~mask) | (nw & mask));
        X_M32(esp + 8u) = (old & ~mask) | (nw & mask);          /* the guest wrote its arg slot; kept for fidelity */
        c->r[0] = (uint32_t)(int32_t)(int16_t)old;
        c->r[4] = esp + 4u; crt_calls[0]++; return 1;
    }
    case 1: {
        double v = x87_load_f64(c, esp + 4u);
        x87_push(c, x87_round(c, v));
        c->r[4] = esp + 4u; crt_calls[1]++; return 1;
    }
    case 2: {
        uint32_t hi = X_M32(esp + 8u);
        if ((hi & 0x7FF00000u) == 0x7FF00000u) return 0;      /* NaN / inf: guest path (matherr) */
        double v = x87_load_f64(c, esp + 4u);
        uint16_t old = c->fcw;
        c->fcw = (uint16_t)X_M32(0x1F2840u);
        double r = x87_round(c, v);
        c->fcw = old;
        if (r != v && !(old & 0x20u)) return 0;                  /* inexact unmasked: guest raises through _except2 */
        x87_push(c, r);
        c->r[0] = (uint32_t)(int32_t)(int16_t)old;              /* eax: the last _controlfp's return */
        c->r[4] = esp + 4u; crt_calls[2]++; return 1;
    }
    case 3: {   /* f_00061270: float3 at [esp+4] -> 11/11/10-bit packed integer in eax; a transcription of the emitted body with the
                 * emulator's own x87 primitives (compare flags, rounding under the 0x1F2840 word, fistp), minus the guest-stack traffic and
                 * the calls. XV_NATIVE_PACK: 0 off, 1 verify against the guest body (guest result used), 2 native. */
        static int mode = -1; if (mode < 0) { const char *e = getenv("XV_NATIVE_PACK"); mode = e ? atoi(e) : 0; }
        if (!mode) return 0;
        static int in_body; if (in_body) return 0;
        uint32_t ptr = X_M32(esp + 4u);
        uint32_t packed = 0;
        for (int i = 0; i < 3; ++i) {
            double v = x87_load_f32(c, ptr + 4u * i), x;
            x87_compare(c, v, x87_load_f32(c, 0x1F0ABCu), 0);
            unsigned r1 = ((c->fsw >> 8) & 0xFFu) & 0x5u; int pf1 = (__builtin_parity(r1) == 0);
            if (!pf1) x = x87_load_f32(c, 0x1F0ABCu);
            else {
                x87_compare(c, v, x87_load_f32(c, 0x1F0A78u), 0);
                unsigned r2 = ((c->fsw >> 8) & 0xFFu) & 0x41u;
                x = r2 ? v : x87_load_f32(c, 0x1F0A78u);
            }
            x = x * x87_load_f32(c, i == 2 ? 0x1F0C30u : 0x1F0C34u);
            double fl; { uint16_t old = c->fcw; c->fcw = (uint16_t)X_M32(0x1F2840u); fl = x87_round(c, x); c->fcw = old; }   /* f_00019E7B */
            float st = (float)fl;                                              /* fstp dword */
            double q = x87_round(c, (double)st);                               /* fistp */
            uint32_t iv = (q >= -2147483648.0 && q <= 2147483647.0) ? (uint32_t)(int32_t)q : 0x80000000u;
            packed |= (iv & 0x7FFu) << (11 * i);   /* guest: eax=z; shl 11; or y; shl 11; or x */
        }
        if (mode == 1) {
            xctx copy = *c; copy.preempt = 1 << 30; in_body = 1; extern void f_00061270(xctx *); f_00061270(&copy); in_body = 0;
            static unsigned calls, mismatches, shown; calls++;
            if (copy.r[0] != packed) { mismatches++; if (shown < 6) { shown++; float v[3]; for (int i = 0; i < 3; ++i) { uint32_t w = X_M32(ptr + 4u * i); memcpy(&v[i], &w, 4); } XK_LOG("[native-pack] MISMATCH in %g %g %g guest %08X native %08X\n", v[0], v[1], v[2], copy.r[0], packed); } }
            if ((calls & 0x3FFF) == 0) XK_LOG("[native-pack] verify: %u calls, %u mismatches\n", calls, mismatches);
            int32_t pre = c->preempt; *c = copy; c->preempt = pre; return 1;   /* keep the real thread's preempt budget */
        }
        c->r[0] = packed; c->r[4] = esp + 8u; crt_calls[3]++; return 1;
    }
    }
    return 0;
}
void xv_crt_float_report(unsigned frames)
{
    if (!crt_calls[0] && !crt_calls[1] && !crt_calls[2] && !crt_calls[3]) return;
    XK_LOG("[crt-float] %u frames: native _controlfp %u, _frnd %u, floor %u, pack %u\n", frames, crt_calls[0], crt_calls[1], crt_calls[2], crt_calls[3]);
    crt_calls[0] = crt_calls[1] = crt_calls[2] = crt_calls[3] = 0;
}
