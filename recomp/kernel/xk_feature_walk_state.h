/* Native 86A40 polygon walk, not a runtime hook. Caller proves the supplied
 * geometry matches EDI, stable mappings, valid stack/output spans and physical
 * disjointness of all writes from inputs. No yields inside an admitted walk. */
#ifndef XK_FEATURE_WALK_STATE_H
#define XK_FEATURE_WALK_STATE_H
#include "xk_feature_state.h"
static inline int xk_feature_walk_state(xctx *c,const xk_feature_geometry *g,
                                       uint8_t *output){
    uint32_t sp=c->r[4],surface=X_M32(sp+4);
    if(surface>=g->surface_count)return 0;
    uint32_t first=xk_fb_u32(g->surfaces+(size_t)surface*12+4),edge=first,last_vertex=0;
    uint8_t points[8][12];unsigned count=0;
    do {
        if(edge>=g->edge_count||count==8)return 0;
        const uint8_t *e=g->edges+(size_t)edge*24;
        unsigned side=xk_fb_u32(e+20)==surface;
        if(!side&&xk_fb_u32(e+16)!=surface)return 0;
        last_vertex=xk_fb_u32(e+4*side);
        if(last_vertex>=g->vertex_count)return 0;
        memcpy(points[count++],g->vertices+(size_t)last_vertex*16,12);
        edge=xk_fb_u32(e+8+side*4);
    }while(edge!=first);
    if(c->preempt<(int32_t)count)return 0;
    X_PUSH32(c->r[1]);X_PUSH32(c->r[3]);X_PUSH32(c->r[5]);X_PUSH32(c->r[6]);
    X_W32(sp-4)=first;
    memcpy(output,points,count*12);
#ifdef XV_CHECK_GUEST_ADDRESS
    xv_mark_written(output,count*12);
#endif
    /* Only the last shift/carry, increment and comparison survive the walk.
     * Preserve retired stack bytes and all original backedge decrements. */
    (void)x_shl32(c,last_vertex,4);
    c->r[0]=count-1;xk_feature_inc_state(c,0);
    xk_feature_cmp_state(c,first,first);c->r[2]=first;
    c->preempt-=(int32_t)(count-1);
    c->r[6]=X_POP32();c->r[5]=X_POP32();c->r[3]=X_POP32();c->r[1]=X_POP32();
    c->r[4]+=12;return 1;
}
#endif
