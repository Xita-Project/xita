/* Native emitter admission. No installed runtime hook.
 * Caller supplies the actual arena/page-table extents and guarantees mapping
 * and allocation lifetime throughout this non-yielding call. */
#ifndef XK_FEATURE_ADMISSION_H
#define XK_FEATURE_ADMISSION_H
#include "xk_feature_memory.h"
#include "xk_feature_state.h"

static inline int xk_feature_admit_span(uint64_t arena_bytes,size_t page_count,
        uint32_t address,size_t bytes,xk_feature_span *span){
    if(!xk_feature_map_span(g_xram,arena_bytes,X_PT,page_count,address,bytes,span))return 0;
#ifdef XV_CHECK_GUEST_ADDRESS
    uint32_t last=(uint32_t)(((uint64_t)address+bytes-1)>>12);
    for(uint32_t page=address>>12;page<=last;page++)
        if(XV_ADDRESS_NEEDS_POLICY(page<<12,X_PT[page]))return 0;
    /* Diagnostic watches require original ordered stores. */
    xk_feature_span watch={NULL,xv_watch_off,xv_watch_len};
    if(xk_feature_spans_overlap(span,&watch))return 0;
#endif
    return 1;
}

static inline int xk_feature_try_vertex_emit(xctx *c,uint64_t arena_bytes,size_t page_count){
    /* Reject render-view mappings; caller must also own the live allocation. */
    if(X_PT!=g_xpt || c->fcw!=0x37f || c->r[4]<8 || c->r[4]>UINT32_MAX-28)return 0;
    xk_feature_span output,stack,reads[3];
    if(!xk_feature_admit_span(arena_bytes,page_count,c->r[1],0x4408,&output) ||
       !xk_feature_admit_span(arena_bytes,page_count,c->r[4]-8,8,&stack) ||
       !xk_feature_admit_span(arena_bytes,page_count,c->r[4],28,&reads[0]) ||
       !xk_feature_admit_span(arena_bytes,page_count,c->r[6],12,&reads[1]) ||
       !xk_feature_admit_span(arena_bytes,page_count,0x1f0a68,4,&reads[2]) ||
       !xk_feature_layout_disjoint(&output,&stack,reads,3))return 0;
    uint32_t zero;memcpy(&zero,reads[2].host,4);
    if(zero!=0)return 0;
    if(!xk_feature_vertex_emit_state(c,output.host))return 0;
#ifdef XV_CHECK_GUEST_ADDRESS
    /* Conservative output coverage also invalidates derived-data caches. */
    xv_mark_written(output.host,(uint32_t)output.bytes);
#endif
    return 1;
}
#endif
