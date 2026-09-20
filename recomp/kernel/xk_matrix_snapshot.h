/* Pure matrix arithmetic for owned snapshots. No guest reads or locking. */
#ifndef XITA_MATRIX_SNAPSHOT_H
#define XITA_MATRIX_SNAPSHOT_H
#include "../xv_x86rt.h"
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

static inline void xv_matrix_snapshot_vectors(const float l[13],const float r[13],
    float op[13],float v[8][4])
{
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
}
static inline void xv_matrix_snapshot_finish(xctx *c,const float l[13],
    const float r[13],float op[13],const float v[8][4])
{
    double scale;
#if defined(__arm__)
    /* Preserve the previous compiled scale product's NaN operand priority. */
    double left=(double)l[0],right=(double)r[0];
    __asm__("vmul.f64 %P0, %P1, %P2" : "=w"(scale) : "w"(right),"w"(left));
#else
    scale = (double)l[0] * (double)r[0];
#endif
    op[0]=(float)scale;
    memcpy(c->xmm, v, sizeof c->xmm);
    c->st[(c->fsp - 1u) & 7u] = scale;
}
static inline void xv_matrix_snapshot(xctx *c,const float l[13],const float r[13],float op[13])
{
    float v[8][4];
    xv_matrix_snapshot_vectors(l,r,op,v);
    xv_matrix_snapshot_finish(c,l,r,op,v);
}
#endif
