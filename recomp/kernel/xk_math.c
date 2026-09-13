/* Halo 3925 leaf math. Keep the original operation order and precision;
 * unusual layouts use the unchanged lifted function without side effects. */
#include "xk.h"
#include <stdlib.h>

static unsigned math_fast[2], math_fallback[2];
enum { ML_DISJOINT, ML_LEFT, ML_RIGHT, ML_BOTH, ML_DISABLED, ML_ALIGNMENT,
       ML_PAGE, ML_SCRATCH, ML_PARTIAL, ML_COUNT };
static unsigned matrix_layout[ML_COUNT];
static int matrix_decline(unsigned reason)
{ math_fallback[0]++; matrix_layout[reason]++; return 0; }
static int math_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        const char *e = getenv("XV_NATIVE_MATH");
        enabled = !e || atoi(e) != 0;
        XK_LOG("native matrix/quaternion math: %s\n", enabled ? "on" : "off");
    }
    return enabled;
}
static void *math_span(uint32_t address, unsigned bytes)
{
    if ((address & 3u) || bytes > 4096u - (address & 4095u)) return NULL;
    return X_G(address);
}
static int math_overlap(const void *a, unsigned an, const void *b, unsigned bn)
{
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x < y + bn && y < x + an;
}
void xv_native_math_report(unsigned frames)
{
#ifdef XV_NATIVE_MODEL_PALETTE
    extern void xv_model_palette_report(unsigned);
    xv_model_palette_report(frames);
#endif
    extern unsigned xv_math_bounds_calls(void) __attribute__((weak));
    if (xv_math_bounds_calls) XK_LOG("[native-bounds] %u frames: %u calls\n", frames, xv_math_bounds_calls());
    extern unsigned xv_math_clip_calls(void) __attribute__((weak));
    extern unsigned xv_math_clip_register_calls(void) __attribute__((weak));
    if (xv_math_clip_calls) XK_LOG("[native-clip] %u frames: %u calls\n", frames, xv_math_clip_calls());
    if (xv_math_clip_register_calls) XK_LOG("[clip-registers] %u frames: %u calls\n", frames, xv_math_clip_register_calls());
    XK_LOG("[native-math] %u frames matrix %u fast / %u fallback; quaternion %u fast / %u fallback\n",
        frames, math_fast[0], math_fallback[0], math_fast[1], math_fallback[1]);
    memset(math_fast, 0, sizeof math_fast);
    memset(math_fallback, 0, sizeof math_fallback);
    XK_LOG("[matrix-layout] %u frames native disjoint %u left %u right %u both %u; fallback disabled %u alignment %u page %u scratch %u partial %u\n",
        frames,matrix_layout[0],matrix_layout[1],matrix_layout[2],matrix_layout[3],
        matrix_layout[4],matrix_layout[5],matrix_layout[6],matrix_layout[7],matrix_layout[8]);
    memset(matrix_layout,0,sizeof matrix_layout);
}

int xv_math_matrix_multiply(xctx *restrict c)
{
    uint32_t sp = c->r[4];
    uint32_t *stack = math_span(sp - 16u, 32);
    if (!math_enabled()) return matrix_decline(ML_DISABLED);
    if (!stack) return matrix_decline((sp&3u)?ML_ALIGNMENT:ML_PAGE);
    uint32_t a = stack[5], b = stack[6], out = stack[7];
    const float *ap = math_span(a, 52), *bp = math_span(b, 52);
    float *op = math_span(out, 52);
    if (!ap || !bp || !op) return matrix_decline(((a|b|out)&3u)?ML_ALIGNMENT:ML_PAGE);
    if (math_overlap(stack,32,ap,52) || math_overlap(stack,32,bp,52) || math_overlap(stack,32,op,52))
        return matrix_decline(ML_SCRATCH);
    /* The original consumes each rotation input before an exact in-place
     * result overwrites it, then reads untouched translations and scales.
     * Partial overlap has different read/write dependencies: retain the lift.
     * Compare host addresses so guest physical/virtual aliases count too. */
    if ((op!=ap && math_overlap(ap,52,op,52)) || (op!=bp && math_overlap(bp,52,op,52)))
        return matrix_decline(ML_PARTIAL);
    matrix_layout[(op==ap?ML_LEFT:0)+(op==bp?ML_RIGHT:0)]++;
    float l[13], r[13], v[8][4];
    memcpy(l, ap, sizeof l); memcpy(r, bp, sizeof r);
    /* SSE lanes are x, zero, y, z. Preserve even the otherwise unused lane,
     * including NaN/zero behavior, and each float rounding point. */
    for (unsigned i = 0; i < 3; i++) {
        v[i][0] = l[1+i*3]; v[i][1] = 0;
        v[i][2] = l[2+i*3]; v[i][3] = l[3+i*3];
    }
    for (unsigned row = 0; row < 3; row++) {
        float q[4];
        for (unsigned j = 0; j < 4; j++) {
            float first = r[1+row*3] * v[0][j];
            float second = r[2+row*3] * v[1][j];
            float third = r[3+row*3] * v[2][j];
            q[j] = (first + second) + third;
        }
        op[1+row*3] = q[0]; op[2+row*3] = q[2]; op[3+row*3] = q[3];
        if (row == 2) { v[7][0]=q[3]; v[7][1]=q[3]; v[7][2]=q[0]; v[7][3]=q[2]; }
    }
    v[6][0]=l[10]; v[6][1]=0; v[6][2]=l[11]; v[6][3]=l[12];
    for (unsigned j = 0; j < 4; j++) {
        float first = r[10] * v[0][j], third = r[12] * v[2][j];
        v[4][j] = r[11] * v[1][j];
        v[5][j] = l[0];
        float translation = ((first + v[4][j]) + third) * v[5][j];
        v[3][j] = translation + v[6][j];
    }
    op[10]=v[3][0]; op[11]=v[3][2]; op[12]=v[3][3];
    double scale = (double)l[0] * (double)r[0];
    op[0]=(float)scale;
    memcpy(c->xmm, v, sizeof v);
    c->st[(c->fsp - 1u) & 7u] = scale;
    stack[0]=a; stack[1]=out+4; stack[2]=b+4; stack[3]=a+4;
    c->r[0]=b; c->r[1]=out; c->r[2]=a; c->r[4]=sp+16;
    math_fast[0]++;
    return 1;
}

/* 0xB5F60: quaternion to scaled transform. Fixed native temporaries replace
 * x87 stack dispatch; float scratch spills and final x87 slots are retained. */
int xv_math_quaternion_matrix(xctx *restrict c)
{
    uint32_t sp=c->r[4], fp=c->fsp;
    const float *ip=math_span(c->r[1],16);
    float *output=math_span(c->r[2],52), *scratch_out=math_span(sp-24u,24);
    const void *constants = math_span(0x1F0A68u, 0xA0u);
    if (!math_enabled() || !ip || !output || !scratch_out ||
        math_overlap(ip,16,output,52) || math_overlap(ip,16,scratch_out,24) ||
        math_overlap(output,52,scratch_out,24) || math_overlap(output,52,constants,0xA0u) ||
        math_overlap(scratch_out,24,constants,0xA0u)) {
        math_fallback[1]++; return 0;
    }
    float input[4], scratch[6];
    memcpy(input,ip,sizeof input);
    double s0=c->st[(fp+0)&7u];
    double s1=c->st[(fp+1)&7u];
    double s2=c->st[(fp+2)&7u];
    double s3=c->st[(fp+3)&7u];
    double s4=c->st[(fp+4)&7u];
    double s5=c->st[(fp+5)&7u];
    double s6=c->st[(fp+6)&7u];
    double s7=c->st[(fp+7)&7u];
    s7 = (double)input[3];
    s6 = (double)input[2];
    s5 = (double)input[1];
    s4 = (double)input[0];
    s3 = s4;
    s3 = s3 * s4;
    s2 = s5;
    s2 = s2 * s5;
    s3 = s3 + s2;
    s2 = s6;
    s2 = s2 * s6;
    s3 = s3 + s2;
    s2 = s7;
    s2 = s2 * s7;
    s3 = s3 + s2;
    s7 = s3;
    c->fsp=(fp+7u)&7u;
    x87_compare(c, s7, x87_load_f32(c, (0x1F0A68u)), 0);
    c->fsp=fp;
    X_R16(0) = c->fsw;
    { uint8_t r_ = X_R8H(0) & 0x44u; X_FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    if (!XF_P(c)) goto L_000B5FA1;
    s7 = x87_load_f32(c, (0x1F0B04u)) / s7;
    goto L_000B5FA9;
L_000B5FA1:
    s7 = x87_load_f32(c, (0x1F0A68u));
L_000B5FA9:
    s6 = s7;
    { uint32_t a_ = c->r[0], b_ = c->r[0]; uint32_t r_ = (uint32_t)(a_ ^ b_); c->r[0] = r_; }
    s6 = s6 * (double)input[0];
    s5 = s7;
    s5 = s5 * (double)input[1];
    scratch[1] = (float)(s5);
    { double t_ = s6; s6 = s7; s7 = t_; }
    s6 = s6 * (double)input[2];
    scratch[0] = (float)(s6);
    s6 = s7;
    s6 = s6 * (double)input[3];
    scratch[4] = (float)(s6);
    s6 = (double)scratch[1];
    s6 = s6 * (double)input[3];
    scratch[3] = (float)(s6);
    s6 = (double)scratch[0];
    s6 = s6 * (double)input[3];
    scratch[2] = (float)(s6);
    s7 = s7 * (double)input[0];
    s6 = (double)scratch[1];
    s6 = s6 * (double)input[0];
    s5 = (double)scratch[0];
    s5 = s5 * (double)input[0];
    s4 = (double)scratch[1];
    s4 = s4 * (double)input[1];
    scratch[5] = (float)(s4);
    s4 = (double)scratch[0];
    s4 = s4 * (double)input[1];
    scratch[1] = (float)(s4);
    s4 = (double)scratch[0];
    s4 = s4 * (double)input[2];
    ((uint32_t *)output)[0] = 0x3F800000u;
    s3 = (double)scratch[5];
    ((uint32_t *)output)[10] = c->r[0];
    ((uint32_t *)output)[11] = c->r[0];
    s3 = s3 + s4;
    ((uint32_t *)output)[12] = c->r[0];
    s3 = x87_load_f32(c, (0x1F0A78u)) - s3;
    output[1] = (float)(s3);
    s3 = s6;
    s3 = s3 - (double)scratch[2];
    output[2] = (float)(s3);
    s3 = s5;
    s3 = s3 + (double)scratch[3];
    output[3] = (float)(s3);
    { double t_ = s4; s4 = s6; s6 = t_; }
    s4 = s4 + (double)scratch[2];
    output[4] = (float)(s4);
    { double t_ = s5; s5 = s6; s6 = t_; }
    s5 = s5 + s7;
    s5 = x87_load_f32(c, (0x1F0A78u)) - s5;
    output[5] = (float)(s5);
    s5 = (double)scratch[1];
    s5 = s5 - (double)scratch[4];
    output[6] = (float)(s5);
    s6 = s6 - (double)scratch[3];
    output[7] = (float)(s6);
    s6 = (double)scratch[1];
    s6 = s6 + (double)scratch[4];
    output[8] = (float)(s6);
    s6 = (double)scratch[5];
    s6 = s6 + s7;
    s6 = x87_load_f32(c, (0x1F0A78u)) - s6;
    output[9] = (float)(s6);
    memcpy(scratch_out,scratch,sizeof scratch);
    c->st[(fp+0)&7u]=s0;
    c->st[(fp+1)&7u]=s1;
    c->st[(fp+2)&7u]=s2;
    c->st[(fp+3)&7u]=s3;
    c->st[(fp+4)&7u]=s4;
    c->st[(fp+5)&7u]=s5;
    c->st[(fp+6)&7u]=s6;
    c->st[(fp+7)&7u]=s7;
    c->r[4]=sp+4;
    math_fast[1]++;
    return 1;
}
