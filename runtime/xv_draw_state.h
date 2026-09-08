#ifndef XV_DRAW_STATE_H
#define XV_DRAW_STATE_H
#include <psp2/gxm.h>

/* Local to one uninterrupted mesh replay range. UI draws, clears and scene
 * transitions invalidate this state; uniforms and textures remain per draw. */
typedef struct {
    int valid;
    SceGxmDepthFunc depth;
    SceGxmDepthWriteMode write;
    SceGxmCullMode cull;
    SceGxmVertexProgram *vertex;
    SceGxmFragmentProgram *fragment;
} xv_draw_state;

static inline void xv_draw_state_bind(xv_draw_state *s, SceGxmContext *ctx,
    SceGxmDepthFunc depth, SceGxmDepthWriteMode write, SceGxmCullMode cull,
    SceGxmVertexProgram *vertex, SceGxmFragmentProgram *fragment)
{
    if (!s->valid || s->depth != depth) sceGxmSetFrontDepthFunc(ctx,depth);
    if (!s->valid || s->write != write) sceGxmSetFrontDepthWriteEnable(ctx,write);
    if (!s->valid || s->cull != cull) sceGxmSetCullMode(ctx,cull);
    if (!s->valid || s->vertex != vertex) sceGxmSetVertexProgram(ctx,vertex);
    if (!s->valid || s->fragment != fragment) sceGxmSetFragmentProgram(ctx,fragment);
    *s=(xv_draw_state){1,depth,write,cull,vertex,fragment};
}
#endif
