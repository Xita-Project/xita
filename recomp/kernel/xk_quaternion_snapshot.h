/* Snapshot-only quaternion kernel. The caller supplies owned inputs,
 * output and scratch; this function performs no guest-address translation,
 * synchronization, allocation or global accounting. The context is exclusively
 * owned by the caller. Preserve operation order, precision and spill state. */
#ifndef XITA_QUATERNION_SNAPSHOT_H
#define XITA_QUATERNION_SNAPSHOT_H
#include "../xv_x86rt.h"
static inline double xv_quaternion_float_word(uint32_t word)
{
    float value;memcpy(&value,&word,4);return (double)value;
}
static inline void xv_quaternion_snapshot(xctx *c,const float input[4],
    float output[13],float scratch_out[6],uint32_t zero,uint32_t two,uint32_t one)
{
    unsigned fp=c->fsp;
    float scratch[6];
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
    x87_compare(c, s7, xv_quaternion_float_word(zero), 0);
    c->fsp=fp;
    X_R16(0) = c->fsw;
    { uint8_t r_ = X_R8H(0) & 0x44u; X_FLAGS(XK_LOGIC, 0, 0, r_, 8); }
    if (!XF_P(c)) goto L_000B5FA1;
    s7 = xv_quaternion_float_word(two) / s7;
    goto L_000B5FA9;
L_000B5FA1:
    s7 = xv_quaternion_float_word(zero);
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
    s3 = xv_quaternion_float_word(one) - s3;
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
    s5 = xv_quaternion_float_word(one) - s5;
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
    s6 = xv_quaternion_float_word(one) - s6;
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
}
#endif
