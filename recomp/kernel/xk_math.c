/* Halo 3925 leaf math. Keep the original operation order and precision;
 * unusual layouts use the unchanged lifted function without side effects. */
#include "xk.h"
#include "xk_object_jobs.h"
#include <stdlib.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif
#ifdef XV_QUAT_CACHE
#include "xk_quat_cache.h"
#endif
#ifdef XV_NATIVE_MATRIX_NEON
#include "xk_matrix_neon.h"
#endif

static unsigned math_fast[2], math_fallback[2];
/* Slot zero retains guarded accounting. A bypassed quaternion owns its actual
 * worker's slot; reports aggregate/reset only after every job has retired. */
static struct __attribute__((aligned(64))) {
    unsigned fast, fallback;
} quaternion_stats[3];
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_QUAT_EXPERIMENT) && !defined(XV_QUAT_CACHE)
static unsigned quaternion_configured;
#endif
/* Guarded calls use slot zero; admitted private calls own their worker slot.
 * Reporting occurs after object batches join. No atomic per-operation update. */
static struct __attribute__((aligned(64))) point_counts {
    unsigned fast, fallback[4];
} point_stats[3];
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_POINT_EXPERIMENT)
static unsigned point_configured;
#endif
static int point_override = -1;
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
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && (defined(XV_OBJECT_POINT_EXPERIMENT) || defined(XV_OBJECT_QUAT_EXPERIMENT))
    xv_object_math_report_check();
#endif
    XV_OBJECT_MATH_GUARD();
#ifdef XV_NATIVE_MATRIX_NEON
    matrix_neon_report(frames);
#endif
    unsigned point_fast=0,point_fallback[4]={0};
    for(unsigned lane=0;lane<3;lane++) {
        point_fast+=point_stats[lane].fast;
        for(unsigned i=0;i<4;i++)point_fallback[i]+=point_stats[lane].fallback[i];
    }
    XK_LOG("[native-point] %u frames fast %u; fallback disabled %u fp %u layout %u numeric %u\n",
           frames,point_fast,point_fallback[0],point_fallback[1],point_fallback[2],point_fallback[3]);
    memset(point_stats,0,sizeof point_stats);
#ifdef XV_NATIVE_OBJECT_BASIS
    extern void xv_object_basis_report(unsigned);
    xv_object_basis_report(frames);
#endif
#ifdef XV_QUAT_CACHE
    xv_quat_cache_report(frames);
#endif
#ifdef XV_NATIVE_MODEL_PALETTE
    extern void xv_model_palette_report(unsigned);
    xv_model_palette_report(frames);
#endif
#ifdef XV_NATIVE_MODEL_HIERARCHY
    extern void xv_model_hierarchy_report(unsigned);
    xv_model_hierarchy_report(frames);
#endif
    extern unsigned xv_math_bounds_calls(void) __attribute__((weak));
    if (xv_math_bounds_calls) XK_LOG("[native-bounds] %u frames: %u calls\n", frames, xv_math_bounds_calls());
    extern int xv_native_polygon_edge_available(void) __attribute__((weak));
    extern unsigned xv_math_polygon_edge_calls(void) __attribute__((weak));
    if(xv_native_polygon_edge_available && xv_native_polygon_edge_available() && xv_math_polygon_edge_calls)
        XK_LOG("[polygon-edge] %u frames: %u native calls\n",frames,xv_math_polygon_edge_calls());
    extern void xv_clip_region_report(unsigned) __attribute__((weak));
    if (xv_clip_region_report) xv_clip_region_report(frames);
    extern unsigned xv_math_clip_calls(void) __attribute__((weak));
    extern unsigned xv_math_clip_register_calls(void) __attribute__((weak));
    if (xv_math_clip_calls) XK_LOG("[native-clip] %u frames: %u calls\n", frames, xv_math_clip_calls());
    if (xv_math_clip_register_calls) XK_LOG("[clip-registers] %u frames: %u calls\n", frames, xv_math_clip_register_calls());
    unsigned quaternion_fast=0,quaternion_fallback=0;
    for(unsigned lane=0;lane<3;lane++) {
        quaternion_fast+=quaternion_stats[lane].fast;
        quaternion_fallback+=quaternion_stats[lane].fallback;
    }
    XK_LOG("[native-math] %u frames matrix %u fast / %u fallback; quaternion %u fast / %u fallback\n",
        frames, math_fast[0], math_fallback[0], quaternion_fast, quaternion_fallback);
    memset(quaternion_stats,0,sizeof quaternion_stats);
    memset(math_fast, 0, sizeof math_fast);
    memset(math_fallback, 0, sizeof math_fallback);
    XK_LOG("[matrix-layout] %u frames native disjoint %u left %u right %u both %u; fallback disabled %u alignment %u page %u scratch %u partial %u\n",
        frames,matrix_layout[0],matrix_layout[1],matrix_layout[2],matrix_layout[3],
        matrix_layout[4],matrix_layout[5],matrix_layout[6],matrix_layout[7],matrix_layout[8]);
    memset(matrix_layout,0,sizeof matrix_layout);
}

void xv_point_math_override(int value)
{
    point_override=value<0?-1:!!value;
}
static int point_decline(struct point_counts *stats,unsigned reason)
{
    stats->fallback[reason]++; return 0;
}
static int point_fp_supported(void)
{
#if defined(__arm__)
    uint32_t value;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(value) :: "memory");
    return !(value&0x00009f00u);
#elif defined(__x86_64__)
    return (_mm_getcsr()&0x1f80u)==0x1f80u;
#else
    return 0;
#endif
}
/* Pure point transform: retain double intermediates and float store rounding.
 * All three source coordinates are consumed before any output store. Matrix
 * overlap is declined because the original interleaves matrix reads/stores. */
int xv_math_point_transform(xctx *c)
{
    unsigned lane=0;
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_POINT_EXPERIMENT)
    /* Publish immutable configuration before any lock-free helper reads it.
     * A cold call follows the original guarded initialization path. */
    unsigned ready=__atomic_load_n(&point_configured,__ATOMIC_ACQUIRE);
    if(ready)lane=(unsigned)xv_object_private_point(c);
    int xv_object_math_locked_ __attribute__((cleanup(xv_object_math_unlock))) =
        lane?0:xv_object_math_lock();
#else
    XV_OBJECT_MATH_GUARD();
#endif
    struct point_counts *stats=&point_stats[lane];
    static int enabled=-1;
    if(enabled<0) {
        const char *value=getenv("XV_NATIVE_POINT_MATH");
        enabled=!value||atoi(value)!=0;
    }
    int allowed=math_enabled();
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_POINT_EXPERIMENT)
    if(!ready)__atomic_store_n(&point_configured,1,__ATOMIC_RELEASE);
#endif
    if(!allowed||!(point_override<0?enabled:point_override))return point_decline(stats,0);
    if(!point_fp_supported())return point_decline(stats,1);
    const uint32_t *m=math_span(c->r[1],52), *v=math_span(c->r[2],12);
    float *out=math_span(c->r[0],12);
    if(!m||!v||!out||c->fsp>7||math_overlap(m,52,out,12))return point_decline(stats,2);
    /* Finite inputs avoid changing arithmetic NaN payload/operand priority.
     * Classify integer representations without raising FP exceptions. */
    for(unsigned i=0;i<13;i++)if((m[i]&0x7f800000u)==0x7f800000u)return point_decline(stats,3);
    for(unsigned i=0;i<3;i++)if((v[i]&0x7f800000u)==0x7f800000u)return point_decline(stats,3);
    float mf[13],vf[3];
    memcpy(mf,m,sizeof mf);memcpy(vf,v,sizeof vf);
    uint32_t scale=m[0];
    stats->fast++;
    XV_OBJECT_MATH_PRIVATE(c,0,c->r[0],12,0,0);
    double x=vf[0], y=vf[1], z=vf[2];
    if(scale!=0x3f800000u) {
        double s=mf[0]; x=x*s; y=y*s; z=z*s;
    }
    double first=(z*(double)mf[7]+y*(double)mf[4])+x*(double)mf[1];
    first=first+(double)mf[10];
    double y_last=x*(double)mf[2];
    double second=(z*(double)mf[8]+y*(double)mf[5])+y_last;
    second=second+(double)mf[11];
    double z_middle=y*(double)mf[6], z_last=x*(double)mf[3];
    double third=(z*(double)mf[9]+z_middle)+z_last;
    third=third+(double)mf[12];
    out[0]=(float)first;out[1]=(float)second;out[2]=(float)third;
    unsigned top=c->fsp;
    c->st[(top-1)&7]=third;c->st[(top-2)&7]=z_last;
    c->st[(top-3)&7]=z_middle;c->st[(top-4)&7]=second;
    c->st[(top-5)&7]=y_last;
    c->r[2]=scale;
    X_FLAGS(XK_SUB,scale,0x3f800000u,scale-0x3f800000u,32);
    c->r[4]+=4;return 1;
}

/* Keep the prior native VFP operand order while unrolling fixed-size loops.
 * This also preserves arithmetic NaN signs/payloads. Cortex-A9 VMLA retains
 * the separate multiply/add rounding used by the existing compiled helper. */
#if defined(__arm__)
static float matrix_mul(float a,float b)
{
    float value;
    __asm__("vmul.f32 %0, %1, %2" : "=w"(value) : "w"(a), "w"(b));
    return value;
}
static float matrix_mla(float sum,float a,float b)
{
    __asm__("vmla.f32 %0, %1, %2" : "+w"(sum) : "w"(a), "w"(b));
    return sum;
}
#endif

int xv_math_matrix_multiply(xctx *restrict c)
{
    XV_OBJECT_MATH_GUARD();
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
#ifdef XV_NATIVE_MATRIX_NEON
    int use_neon=matrix_neon_admit(l,r);
#endif
    math_fast[0]++;
    XV_OBJECT_MATH_PRIVATE(c,1,out,52,sp-16u,32);
#ifdef XV_NATIVE_MATRIX_NEON
    if (use_neon) matrix_neon_run(l,r,op,v);
    else {
#endif
    /* SSE lanes are x, zero, y, z. Preserve even the otherwise unused lane,
     * including NaN/zero behavior, and each float rounding point. */
    for (unsigned i = 0; i < 3; i++) {
        v[i][0] = l[1+i*3]; v[i][1] = 0;
        v[i][2] = l[2+i*3]; v[i][3] = l[3+i*3];
    }
    for (unsigned row = 0; row < 3; row++) {
        float q[4];
        for (unsigned j = 0; j < 4; j++) {
#if defined(__arm__)
            float first=matrix_mul(r[1+row*3],v[0][j]);
            q[j]=matrix_mla(matrix_mla(first,r[2+row*3],v[1][j]),r[3+row*3],v[2][j]);
#else
            float first = r[1+row*3] * v[0][j];
            float second = r[2+row*3] * v[1][j];
            float third = r[3+row*3] * v[2][j];
            q[j] = (first + second) + third;
#endif
        }
        op[1+row*3] = q[0]; op[2+row*3] = q[2]; op[3+row*3] = q[3];
        if (row == 2) { v[7][0]=q[3]; v[7][1]=q[3]; v[7][2]=q[0]; v[7][3]=q[2]; }
    }
    v[6][0]=l[10]; v[6][1]=0; v[6][2]=l[11]; v[6][3]=l[12];
    for (unsigned j = 0; j < 4; j++) {
#if defined(__arm__)
        /* The previous ARM helper accumulated the middle product first. */
        v[4][j]=matrix_mul(r[11],v[1][j]);
        v[5][j]=l[0];
        float sum=matrix_mla(v[4][j],r[10],v[0][j]);
        sum=matrix_mla(sum,r[12],v[2][j]);
        v[3][j]=matrix_mla(v[6][j],sum,l[0]);
#else
        float first = r[10] * v[0][j], third = r[12] * v[2][j];
        v[4][j] = r[11] * v[1][j];
        v[5][j] = l[0];
        float translation = ((first + v[4][j]) + third) * v[5][j];
        v[3][j] = translation + v[6][j];
#endif
    }
    op[10]=v[3][0]; op[11]=v[3][2]; op[12]=v[3][3];
#ifdef XV_NATIVE_MATRIX_NEON
    }
#endif
    double scale;
#if defined(__arm__)
    /* Preserve the previous compiled scale product's NaN operand priority. */
    double left=(double)l[0],right=(double)r[0];
    __asm__("vmul.f64 %P0, %P1, %P2" : "=w"(scale) : "w"(right),"w"(left));
#else
    scale = (double)l[0] * (double)r[0];
#endif
    op[0]=(float)scale;
    memcpy(c->xmm, v, sizeof v);
    c->st[(c->fsp - 1u) & 7u] = scale;
    stack[0]=a; stack[1]=out+4; stack[2]=b+4; stack[3]=a+4;
    c->r[0]=b; c->r[1]=out; c->r[2]=a; c->r[4]=sp+16;
    return 1;
}

/* Preserve conversion at the original operation, after taking an integer
 * snapshot of shared constants under the guard. */
static double math_float_word(uint32_t word)
{
    float value;memcpy(&value,&word,4);return (double)value;
}

/* 0xB5F60: quaternion to scaled transform. Fixed native temporaries replace
 * x87 stack dispatch; float scratch spills and final x87 slots are retained. */
int xv_math_quaternion_matrix(xctx *restrict c)
{
    unsigned lane=0;
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_QUAT_EXPERIMENT) && !defined(XV_QUAT_CACHE)
    unsigned ready=__atomic_load_n(&quaternion_configured,__ATOMIC_ACQUIRE);
    if(ready)lane=(unsigned)xv_object_private_quaternion(c);
    int xv_object_math_locked_ __attribute__((cleanup(xv_object_math_unlock))) =
        lane?0:xv_object_math_lock();
#else
    XV_OBJECT_MATH_GUARD();
#endif
    int allowed=math_enabled();
#if defined(XV_EXPERIMENTAL_OBJECT_JOBS) && defined(XV_OBJECT_QUAT_EXPERIMENT) && !defined(XV_QUAT_CACHE)
    if(!ready)__atomic_store_n(&quaternion_configured,1,__ATOMIC_RELEASE);
#endif
    uint32_t sp=c->r[4], fp=c->fsp;
    const float *ip=math_span(c->r[1],16);
    float *output=math_span(c->r[2],52), *scratch_out=math_span(sp-24u,24);
    const void *constants = math_span(0x1F0A68u, 0xA0u);
    if (!allowed || !ip || !output || !scratch_out || !constants ||
        math_overlap(ip,16,output,52) || math_overlap(ip,16,scratch_out,24) ||
        math_overlap(output,52,scratch_out,24) || math_overlap(output,52,constants,0xA0u) ||
        math_overlap(scratch_out,24,constants,0xA0u)) {
        quaternion_stats[lane].fallback++; return 0;
    }
#ifdef XV_QUAT_CACHE
    xv_quat_cache_request cache_request;
    if (xv_quat_cache_restore(c,ip,output,scratch_out,constants,&cache_request)) {
        c->r[4]=sp+4; quaternion_stats[lane].fast++; return 1;
    }
#endif
    float input[4], scratch[6];
    memcpy(input,ip,sizeof input);
    uint32_t zero=X_M32(0x1F0A68u),two=X_M32(0x1F0B04u),one=X_M32(0x1F0A78u);
    quaternion_stats[lane].fast++;
#ifndef XV_QUAT_CACHE
    /* The optional shared cache owns a larger transaction; keep its guard. */
    XV_OBJECT_MATH_PRIVATE(c,2,c->r[2],52,sp-24u,24);
#endif
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
    x87_compare(c, s7, math_float_word(zero), 0);
    c->fsp=fp;
    X_R16(0) = c->fsw;
    { uint8_t r_ = X_R8H(0) & 0x44u; X_FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    if (!XF_P(c)) goto L_000B5FA1;
    s7 = math_float_word(two) / s7;
    goto L_000B5FA9;
L_000B5FA1:
    s7 = math_float_word(zero);
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
    s3 = math_float_word(one) - s3;
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
    s5 = math_float_word(one) - s5;
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
    s6 = math_float_word(one) - s6;
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
#ifdef XV_QUAT_CACHE
    xv_quat_cache_store(c,output,scratch_out,&cache_request);
#endif
    c->r[4]=sp+4;
    return 1;
}
