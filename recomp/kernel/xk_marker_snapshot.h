/* A1F5F..A1F9A marker-record arithmetic, over caller-owned snapshots.
 * This is not an admission or publication API. The future bridge must establish
 * private output/scratch, disjoint sources and source lifetime under the guard.
 * No guest addresses below are dereferenced; they preserve guest continuation.
 */
#ifndef XITA_MARKER_SNAPSHOT_H
#define XITA_MARKER_SNAPSHOT_H
#include "xk_quaternion_snapshot.h"
#include "xk_matrix_snapshot.h"
typedef struct {
    float quaternion[4],translation[3],matrix[13];
    uint32_t matrix_base,zero,two,one;
} xv_marker_input;
typedef struct {
    uint16_t node;
    float local[13],world[13];
    uint32_t spills[8];
} xv_marker_result;

static inline void xv_marker_snapshot(xctx *c,const xv_marker_input *in,
    xv_marker_result *out)
{
    uint32_t destination=c->r[6];
    out->node=(uint16_t)c->r[0];
    c->r[2]=destination+4u;c->r[1]=c->r[7]+16u;
    float scratch[6];
    xv_quaternion_snapshot(c,in->quaternion,out->local,scratch,
        in->zero,in->two,in->one);
    memcpy(out->local+10,in->translation,12);
    c->r[7]=in->matrix_base;
    c->r[1]=(uint32_t)(int32_t)(int16_t)out->node;
    c->r[1]=x_imul32(c,c->r[1],52u);
    uint32_t offset=c->r[1],base=c->r[7],matrix=offset+base;
    /* This build's lifted ADD has dead flags. Retain the preceding IMUL's
     * lazy-flag representation for exact continuation-state equivalence. */
    xv_matrix_snapshot(c,in->matrix,out->local,out->world);
    out->spills[0]=matrix;out->spills[1]=destination+60u;
    out->spills[2]=destination+8u;out->spills[3]=matrix+4u;
    out->spills[4]=0xA1F9Au;out->spills[5]=matrix;
    out->spills[6]=destination+4u;out->spills[7]=destination+56u;
    c->r[0]=destination+4u;c->r[1]=destination+56u;c->r[2]=matrix;
    /* r4 is unchanged across both complete guest calls. Publication preserves
     * destination bytes 2..3 and touches only [sp-32,sp) in the guest stack. */
}
#endif
