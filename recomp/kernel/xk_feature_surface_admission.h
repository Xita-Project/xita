/* Layout-only admission. Caller still proves native ownership, host FP mode and
 * stable mappings/allocation lifetime. No runtime hook installed. */
#ifndef XK_FEATURE_SURFACE_ADMISSION_H
#define XK_FEATURE_SURFACE_ADMISSION_H
#include "xk_feature_admission.h"
#include "xk_feature_surface_state.h"
static inline int xk_feature_try_surface_emit(xctx *c,uint64_t arena_bytes,size_t page_count){
    if(X_PT!=g_xpt||c->fcw!=0x37f||c->fsp>7||c->r[4]<16||c->r[4]>UINT32_MAX-48)return 0;
    xk_feature_span args,stack,output,record={0},reads[4];
    if(!xk_feature_admit_span(arena_bytes,page_count,c->r[4],48,&args))return 0;
    uint32_t out,points,plane;uint16_t count;
    memcpy(&out,args.host+44,4);memcpy(&points,args.host+8,4);memcpy(&plane,args.host+12,4);memcpy(&count,args.host+4,2);
    if(count>8||c->preempt<(int)(2*count+1))return 0;
    if(!xk_feature_admit_span(arena_bytes,page_count,out,6,&output)||
       !xk_feature_admit_span(arena_bytes,page_count,c->r[4]-16,64,&stack)||
       !xk_feature_admit_span(arena_bytes,page_count,plane,16,&reads[0])||
       !xk_feature_admit_span(arena_bytes,page_count,0x1f0a68,4,&reads[1])||
       !xk_feature_admit_span(arena_bytes,page_count,0x1eaf30,24,&reads[2]))return 0;
    unsigned n=xk_fb_count(output.host+4);
    if(n>256)return 0;
    if(n<256){
        uint32_t delta=0x4408+104*n;
        if(out>UINT32_MAX-delta ||
           !xk_feature_admit_span(arena_bytes,page_count,out+delta,104,&record) ||
           record.physical!=output.physical+delta)return 0;
    }
    unsigned nreads=3;
    if(count){if(!xk_feature_admit_span(arena_bytes,page_count,points,count*12,&reads[3]))return 0;nreads++;}
    if(!xk_feature_layout_disjoint(&output,&stack,reads,nreads))return 0;
    if(n<256 && (!xk_feature_layout_disjoint(&record,&stack,reads,nreads) ||
                 xk_feature_spans_overlap(&output,&record)))return 0;
    uint32_t zero;memcpy(&zero,reads[1].host,4);if(zero)return 0;
    for(unsigned axis=0;axis<3;axis++)for(unsigned positive=0;positive<2;positive++){
        uint16_t pair[2];memcpy(pair,reads[2].host+(axis*2+positive)*4,4);
        unsigned u=(axis+1)%3,v=(axis+2)%3;
        if(!positive){unsigned temp=u;u=v;v=temp;}
        if(pair[0]!=u||pair[1]!=v)return 0;
    }
    if(!xk_feature_surface_emit_state(c,output.host))return 0;
#ifdef XV_CHECK_GUEST_ADDRESS
    if(n<256){xv_mark_written(output.host+4,2);xv_mark_written(record.host,(uint32_t)record.bytes);}
#endif
    return 1;
}
#endif
