#include "menu_combiner.h"
#include <string.h>

/* Register file indices match the NV2A/D3D combiner source codes. */
enum { R_ZERO = 0, R_C0 = 1, R_C1 = 2, R_FOG = 3, R_V0 = 4, R_V1 = 5,
       R_T0 = 8, R_T1 = 9, R_T2 = 10, R_T3 = 11, R_R0 = 12, R_R1 = 13,
       R_V1R0 = 14, R_EF = 15, R_COUNT = 16 };

void menu_combiner_decode(const h2_command_state *s, menu_combiner *cb)
{
    memset(cb, 0, sizeof *cb);
    for (unsigned i = 0; i < 8; ++i) {
        cb->rgb_in[i] = s->setup[(0xAC0 + i * 4) / 4];
        cb->rgb_out[i] = s->setup[(0x1E40 + i * 4) / 4];
        cb->alpha_in[i] = s->setup[(0x260 + i * 4) / 4];
        cb->alpha_out[i] = s->setup[(0xAA0 + i * 4) / 4];
        cb->factor0[i] = s->setup[(0xA60 + i * 4) / 4];
        cb->factor1[i] = s->setup[(0xA80 + i * 4) / 4];
    }
    cb->final_abcd = s->setup[0x288 / 4];
    cb->final_efg = s->setup[0x28C / 4];
    cb->final_factor0 = s->setup[0x1E20 / 4];
    cb->final_factor1 = s->setup[0x1E24 / 4];
    menu_combiner_prepare(cb);
    uint32_t control = s->setup[0x1E60 / 4];
    cb->stages = control & 0xF;
    if (cb->stages > 8) cb->stages = 8;
    cb->mux_msb = (control & 0x100) != 0;
}

static float sat(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }
static float pos(float x) { return x < 0.0f ? 0.0f : x; }

static float apply_map(unsigned map, float x)
{
    switch (map) {
    case 0x00: return sat(x);                 /* unsigned_identity */
    case 0x20: return 1.0f - sat(x);          /* unsigned_invert */
    case 0x40: return 2.0f * pos(x) - 1.0f;   /* expand_normal */
    case 0x60: return 1.0f - 2.0f * pos(x);   /* expand_negate */
    case 0x80: return pos(x) - 0.5f;          /* halfbias_normal */
    case 0xA0: return 0.5f - pos(x);          /* halfbias_negate */
    case 0xC0: return x;                      /* signed_identity */
    case 0xE0: return -x;                     /* signed_negate */
    default: return x;
    }
}

static void unpack_argb(uint32_t v, float out[4])
{
    out[0] = ((v >> 16) & 0xFF) / 255.0f;
    out[1] = ((v >> 8) & 0xFF) / 255.0f;
    out[2] = (v & 0xFF) / 255.0f;
    out[3] = ((v >> 24) & 0xFF) / 255.0f;
}

/* Read one combiner input into a 3-vector (rgb) or scalar (alpha). */
static void read_in(uint8_t byte, const float reg[R_COUNT][4], int is_alpha, float out[3])
{
    unsigned r = byte & 0xF, chan = byte & 0x10, map = byte & 0xE0;
    if (is_alpha) {
        /* Alpha combiner: channel bit set selects .a (PS_CHANNEL_ALPHA=0x10),
         * clear selects .b (PS_CHANNEL_BLUE=0x00) -- same bit meaning as the RGB
         * combiner's .aaa/.rgb choice. */
        float s = chan ? reg[r][3] : reg[r][2];
        out[0] = apply_map(map, s);
    } else {
        for (unsigned k = 0; k < 3; ++k) {
            float v = chan ? reg[r][3] : reg[r][k]; /* .aaa or .rgb */
            out[k] = apply_map(map, v);
        }
    }
}

static void scale_vec(float *v, unsigned n, unsigned flags)
{
    switch (flags & 0x38) {
    case 0x08: for (unsigned k = 0; k < n; ++k) v[k] -= 0.5f; break;              /* bias */
    case 0x10: for (unsigned k = 0; k < n; ++k) v[k] *= 2.0f; break;             /* x2 */
    case 0x18: for (unsigned k = 0; k < n; ++k) v[k] = (v[k] - 0.5f) * 2.0f; break; /* x2_bias */
    case 0x20: for (unsigned k = 0; k < n; ++k) v[k] *= 4.0f; break;             /* x4 */
    case 0x30: for (unsigned k = 0; k < n; ++k) v[k] *= 0.5f; break;             /* div2 */
    default: break;                                                              /* identity */
    }
}

/* One combiner half (rgb: n=3 / alpha: n=1). Writes ab/cd/sum into reg channels. */
static void combine(uint32_t icw, uint32_t ocw, float reg[R_COUNT][4], int is_alpha,
                    int mux_msb, unsigned chan_off, unsigned n)
{
    float a[3], b[3], c[3], d[3];
    read_in((icw >> 24) & 0xFF, reg, is_alpha, a);
    read_in((icw >> 16) & 0xFF, reg, is_alpha, b);
    read_in((icw >> 8) & 0xFF, reg, is_alpha, c);
    read_in(icw & 0xFF, reg, is_alpha, d);
    unsigned flags = (ocw >> 12) & 0xFF;
    unsigned cd_reg = ocw & 0xF, ab_reg = (ocw >> 4) & 0xF, sum_reg = (ocw >> 8) & 0xF;
    int ab_dot = !is_alpha && (flags & 0x02), cd_dot = !is_alpha && (flags & 0x01);
    float ab[3], cd[3], sum[3];
    if (ab_dot) { float dp = a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; ab[0]=ab[1]=ab[2]=dp; }
    else for (unsigned k = 0; k < n; ++k) ab[k] = a[k] * b[k];
    if (cd_dot) { float dp = c[0]*d[0]+c[1]*d[1]+c[2]*d[2]; cd[0]=cd[1]=cd[2]=dp; }
    else for (unsigned k = 0; k < n; ++k) cd[k] = c[k] * d[k];
    if (flags & 0x04) {                                    /* MUX by spare0 alpha */
        int use_cd = mux_msb ? (reg[R_R0][3] >= 0.5f) : (reg[R_R0][3] >= 0.5f);
        for (unsigned k = 0; k < n; ++k) sum[k] = use_cd ? cd[k] : ab[k];
    } else for (unsigned k = 0; k < n; ++k) sum[k] = ab[k] + cd[k];
    scale_vec(ab, n, flags); scale_vec(cd, n, flags); scale_vec(sum, n, flags);
    if (ab_reg) for (unsigned k = 0; k < n; ++k) reg[ab_reg][chan_off + k] = ab[k];
    if (cd_reg) for (unsigned k = 0; k < n; ++k) reg[cd_reg][chan_off + k] = cd[k];
    if (sum_reg) for (unsigned k = 0; k < n; ++k) reg[sum_reg][chan_off + k] = sum[k];
}

void menu_combiner_eval(const menu_combiner *cb, const float tex[4][4],
                        const float diffuse[4], const float specular[4],
                        const float fog[4], float out[4])
{
    float reg[R_COUNT][4];
    memset(reg, 0, sizeof reg);
    reg[R_ZERO][3] = 0.0f;
    memcpy(reg[R_FOG], fog, 16);
    memcpy(reg[R_V0], diffuse, 16);
    memcpy(reg[R_V1], specular, 16);
    for (unsigned i = 0; i < 4; ++i) memcpy(reg[R_T0 + i], tex[i], 16);
    /* NV2A seeds the spare alpha (r0.a) from texture0 alpha before stage 0. */
    reg[R_R0][3] = tex[0][3];

    for (unsigned i = 0; i < cb->stages; ++i) {
        if (cb->prepared) { memcpy(reg[R_C0], cb->c0f[i], 16); memcpy(reg[R_C1], cb->c1f[i], 16); }
        else { unpack_argb(cb->factor0[i], reg[R_C0]); unpack_argb(cb->factor1[i], reg[R_C1]); }
        combine(cb->rgb_in[i], cb->rgb_out[i], reg, 0, cb->mux_msb, 0, 3);   /* rgb -> [0..2] */
        combine(cb->alpha_in[i], cb->alpha_out[i], reg, 1, cb->mux_msb, 3, 1); /* alpha -> [3] */
    }

    /* Derived final-combiner registers. The final combiner's c0/c1 are its own
     * registers (SET_SPECULAR_FOG_FACTOR0/1), not the last stage's factors: the
     * menu's desaturation pass dots the scene with the stage-0 luma weights and
     * then tints by the final c0, which must not be those same weights. */
    for (unsigned k = 0; k < 4; ++k) reg[R_V1R0][k] = reg[R_V1][k] + reg[R_R0][k];
    if (cb->prepared) { memcpy(reg[R_C0], cb->final_c0f, 16); memcpy(reg[R_C1], cb->final_c1f, 16); }
    else { unpack_argb(cb->final_factor0, reg[R_C0]); unpack_argb(cb->final_factor1, reg[R_C1]); }

    if (!cb->final_abcd && !cb->final_efg) {           /* no final combiner: pass r0 */
        memcpy(out, reg[R_R0], 16);
        for (unsigned k = 0; k < 4; ++k) out[k] = sat(out[k]);
        return;
    }
    /* EF product (register 0xF), computed from the final E,F inputs. */
    float e[3], f[3];
    read_in((cb->final_efg >> 24) & 0xFF, reg, 0, e);
    read_in((cb->final_efg >> 16) & 0xFF, reg, 0, f);
    for (unsigned k = 0; k < 3; ++k) reg[R_EF][k] = e[k] * f[k];

    float fa[3], fb[3], fc[3], fd[3], fg[3];
    read_in((cb->final_abcd >> 24) & 0xFF, reg, 0, fa);
    read_in((cb->final_abcd >> 16) & 0xFF, reg, 0, fb);
    read_in((cb->final_abcd >> 8) & 0xFF, reg, 0, fc);
    read_in(cb->final_abcd & 0xFF, reg, 0, fd);
    read_in((cb->final_efg >> 8) & 0xFF, reg, 1, fg);   /* G -> alpha */
    for (unsigned k = 0; k < 3; ++k)
        out[k] = sat(fa[k] * fb[k] + (1.0f - fa[k]) * fc[k] + fd[k]);
    out[3] = sat(fg[0]);
}

static unsigned input_tex_bits(uint32_t word)
{
    unsigned bits = 0;
    for (unsigned sh = 0; sh < 32; sh += 8) {
        unsigned r = (word >> sh) & 0xF;
        if (r >= 8 && r <= 11) bits |= 1u << (r - 8);
    }
    return bits;
}

void menu_combiner_prepare(menu_combiner *cb)
{
    unsigned used = 0;
    for (unsigned i = 0; i < cb->stages && i < 8; ++i) {
        unpack_argb(cb->factor0[i], cb->c0f[i]);
        unpack_argb(cb->factor1[i], cb->c1f[i]);
        used |= input_tex_bits(cb->rgb_in[i]) | input_tex_bits(cb->alpha_in[i]);
    }
    unpack_argb(cb->final_factor0, cb->final_c0f);
    unpack_argb(cb->final_factor1, cb->final_c1f);
    used |= input_tex_bits(cb->final_abcd) | input_tex_bits(cb->final_efg & 0xFFFFFF00u);
    /* r0.a is seeded from texture 0 alpha before stage 0 (see eval). */
    used |= 1u;
    if (!cb->final_abcd && !cb->final_efg) used |= 1u;
    cb->tex_used = used;
    cb->prepared = 1;
}
