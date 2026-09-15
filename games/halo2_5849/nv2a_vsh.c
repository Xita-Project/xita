#include "nv2a_vsh.h"
#include <math.h>
#include <string.h>

/* Field table (dword, lsb, width) verbatim from dx8_shader_parse.py FIELDS. */
static uint32_t fld(const uint32_t w[4], unsigned dw, unsigned lsb, unsigned width)
{ return (w[dw] >> lsb) & ((1u << width) - 1u); }

#define F_ILU(w)          fld(w,1,25,3)
#define F_MAC(w)          fld(w,1,21,4)
#define F_CONST(w)        fld(w,1,13,8)
#define F_V(w)            fld(w,1,9,4)
#define F_A0X(w)          fld(w,3,1,1)
#define F_OUT_MAC_MASK(w) fld(w,3,24,4)
#define F_OUT_R(w)        fld(w,3,20,4)
#define F_OUT_ILU_MASK(w) fld(w,3,16,4)
#define F_OUT_O_MASK(w)   fld(w,3,12,4)
#define F_OUT_ORB(w)      fld(w,3,11,1)
#define F_OUT_ADDR(w)     fld(w,3,3,8)
#define F_OUT_MUX(w)      fld(w,3,2,1)
#define F_FINAL(w)        fld(w,3,0,1)

enum { MUX_UNUSED = 0, MUX_R = 1, MUX_V = 2, MUX_C = 3 };

typedef struct { float temp[NV2A_VSH_TEMPS][4]; const float (*cst)[4];
                 const float (*in)[4]; float (*out)[4]; int32_t a0; } machine;

/* Read one source operand (slot 0=A,1=B,2=C) with its MUX/negate/swizzle. */
static void read_src(const uint32_t w[4], const machine *m, unsigned slot, float r[4])
{
    static const unsigned neg_lsb[3] = {8, 25, 10};   /* A/B/C_NEG (words 1,2,2) */
    static const unsigned neg_dw[3]  = {1, 2, 2};
    static const unsigned swz_dw[3]  = {1, 2, 2};
    static const unsigned swz_lsb[3] = {6, 23, 8};    /* X lsb; Y/Z/W step down by 2 */
    static const unsigned mux_dw[3]  = {2, 2, 3};
    static const unsigned mux_lsb[3] = {26, 11, 28};
    unsigned mux = fld(w, mux_dw[slot], mux_lsb[slot], 2);
    int neg = (int)fld(w, neg_dw[slot], neg_lsb[slot], 1);
    unsigned r_index;
    if (slot == 0) r_index = fld(w, 2, 28, 4);        /* A_R */
    else if (slot == 1) r_index = fld(w, 2, 13, 4);   /* B_R */
    else r_index = (fld(w, 2, 0, 2) << 2) | fld(w, 3, 30, 2); /* C_R_HIGH:LOW */

    const float *base;
    float zero[4] = {0, 0, 0, 0};
    if (mux == MUX_R) {
        base = (r_index == 12) ? m->out[NV2A_O_POS] : m->temp[r_index & 15];
    } else if (mux == MUX_V) {
        base = m->in[F_V(w) & 15];
    } else if (mux == MUX_C) {
        int idx = (int)F_CONST(w) + (F_A0X(w) ? m->a0 : 0);
        if (idx < 0) idx = 0;
        if (idx > NV2A_VSH_CONSTS - 1) idx = NV2A_VSH_CONSTS - 1;
        base = m->cst[idx];
    } else {
        base = zero;
    }
    for (unsigned k = 0; k < 4; ++k) {
        unsigned s = fld(w, swz_dw[slot], swz_lsb[slot] - 2 * k, 2);
        r[k] = base[s];
    }
    if (neg) for (unsigned k = 0; k < 4; ++k) r[k] = -r[k];
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void mac_op(unsigned op, const float a[4], const float b[4], const float c[4], float d[4])
{
    float dp;
    switch (op) {
    case 1: for (unsigned k = 0; k < 4; ++k) d[k] = a[k]; break;                 /* mov */
    case 2: for (unsigned k = 0; k < 4; ++k) d[k] = a[k] * b[k]; break;           /* mul */
    case 3: for (unsigned k = 0; k < 4; ++k) d[k] = a[k] + c[k]; break;           /* add (A,C) */
    case 4: for (unsigned k = 0; k < 4; ++k) d[k] = a[k] * b[k] + c[k]; break;     /* mad */
    case 5: dp = a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; for (unsigned k=0;k<4;++k) d[k]=dp; break; /* dp3 */
    case 6: dp = a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+b[3]; for (unsigned k=0;k<4;++k) d[k]=dp; break; /* dph */
    case 7: dp = a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3]; for (unsigned k=0;k<4;++k) d[k]=dp; break; /* dp4 */
    case 8: d[0]=1.0f; d[1]=a[1]*b[1]; d[2]=a[2]; d[3]=b[3]; break;               /* dst */
    case 9: for (unsigned k=0;k<4;++k) d[k]=a[k]<b[k]?a[k]:b[k]; break;            /* min */
    case 10: for (unsigned k=0;k<4;++k) d[k]=a[k]>b[k]?a[k]:b[k]; break;           /* max */
    case 11: for (unsigned k=0;k<4;++k) d[k]=a[k]<b[k]?1.0f:0.0f; break;           /* slt */
    case 12: for (unsigned k=0;k<4;++k) d[k]=a[k]>=b[k]?1.0f:0.0f; break;          /* sge */
    default: for (unsigned k=0;k<4;++k) d[k]=0.0f; break;
    }
}

static void ilu_op(unsigned op, const float c[4], float d[4])
{
    float s = c[0], r;
    switch (op) {
    case 1: r = s; break;                                                         /* mov */
    case 2: r = s != 0.0f ? 1.0f / s : 0.0f; break;                               /* rcp */
    case 3: r = s != 0.0f ? clampf(1.0f / s, -1.884467e19f, 1.884467e19f) : 0.0f; break; /* rcc */
    case 4: r = s != 0.0f ? 1.0f / sqrtf(fabsf(s)) : 0.0f; break;                 /* rsq */
    case 5: r = exp2f(s); break;                                                  /* exp */
    case 6: r = s != 0.0f ? log2f(fabsf(s)) : -1.0e30f; break;                    /* log */
    case 7: /* lit */
        d[0] = 1.0f;
        d[1] = c[0] > 0.0f ? c[0] : 0.0f;
        d[2] = c[0] > 0.0f ? powf(c[1] > 0.0f ? c[1] : 0.0f, clampf(c[3], -128.0f, 128.0f)) : 0.0f;
        d[3] = 1.0f;
        return;
    default: r = 0.0f; break;
    }
    d[0] = d[1] = d[2] = d[3] = r;
}

static void write_mask(float dst[4], const float src[4], unsigned mask)
{
    if (mask & 0x8) dst[0] = src[0];
    if (mask & 0x4) dst[1] = src[1];
    if (mask & 0x2) dst[2] = src[2];
    if (mask & 0x1) dst[3] = src[3];
}

/* External o-register / const-bank write shared by MAC and ILU via OUT_MUX. */
static void write_external(machine *m, const uint32_t w[4], const float src[4])
{
    unsigned mask = F_OUT_O_MASK(w);
    if (!mask || F_OUT_ORB(w) != 1) return; /* only o-register writes are honored */
    unsigned idx = F_OUT_ADDR(w) & 0xF;
    write_mask(m->out[idx], src, mask);
    if (idx == NV2A_O_POS) return; /* r12 alias stays consistent via read_src */
}

unsigned nv2a_vsh_run(const uint32_t (*program)[4], unsigned start, unsigned count,
                      const float input[NV2A_VSH_INPUTS][4],
                      const float constant[NV2A_VSH_CONSTS][4],
                      float output[NV2A_VSH_OUTPUTS][4])
{
    machine m;
    memset(m.temp, 0, sizeof m.temp);
    m.cst = constant; m.in = input; m.out = output; m.a0 = 0;
    for (unsigned i = 0; i < NV2A_VSH_OUTPUTS; ++i) { output[i][0] = output[i][1] = output[i][2] = 0.0f; output[i][3] = 1.0f; }

    unsigned ran = 0;
    for (unsigned pc = start; pc < count; ++pc, ++ran) {
        const uint32_t *w = program[pc];
        unsigned mac = F_MAC(w), ilu = F_ILU(w), out_mux = F_OUT_MUX(w);
        float a[4], b[4], c[4];
        read_src(w, &m, 0, a);
        read_src(w, &m, 1, b);
        read_src(w, &m, 2, c);
        float mac_res[4] = {0, 0, 0, 0}, ilu_res[4] = {0, 0, 0, 0};
        if (mac && mac != 13) mac_op(mac, a, b, c, mac_res);
        if (ilu) ilu_op(ilu, c, ilu_res);

        if (mac == 13) {                                    /* arl -> a0.x = floor(a.x) */
            m.a0 = (int32_t)floorf(a[0]);
        } else if (mac) {
            unsigned mm = F_OUT_MAC_MASK(w), rd = F_OUT_R(w) & 15;
            if (mm) write_mask(rd == 12 ? m.out[NV2A_O_POS] : m.temp[rd], mac_res, mm); /* r12 aliases oPos */
            if (out_mux == 0) write_external(&m, w, mac_res);
        }
        if (ilu) {
            unsigned im = F_OUT_ILU_MASK(w);
            if (im) write_mask(m.temp[1], ilu_res, im);     /* ILU temp write is hard-wired to r1 */
            if (out_mux == 1) write_external(&m, w, ilu_res);
        }
        if (F_FINAL(w)) { ++ran; break; }
    }
    return ran;
}
