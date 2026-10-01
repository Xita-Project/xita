/* Query-scoped vertex admission. Caller guarantees allocation ownership and
 * stable live mappings while the vertex pass runs; no runtime hook installed. */
#ifndef XK_FEATURE_SCOPE_H
#define XK_FEATURE_SCOPE_H
#include "xk_feature_admission.h"
typedef struct {
    uint8_t *output;
    xk_feature_guest_call original_emit;
} xk_feature_vertex_scope;
static inline uint32_t xk_feature_read32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}
static inline int xk_feature_prepare_vertex_scope(xctx *c,uint64_t arena_bytes,
        size_t page_count,xk_feature_guest_call original_emit,xk_feature_vertex_scope *result){
    if(X_PT!=g_xpt||c->fcw!=0x37f||c->fsp>7||c->r[4]<88||c->r[4]>UINT32_MAX-24)return 0;
    xk_feature_span stack,output,reads[7],args,header;
    uint32_t sp=c->r[4];
    if(!xk_feature_admit_span(arena_bytes,page_count,sp,24,&args)||
       !xk_feature_admit_span(arena_bytes,page_count,c->r[7],0x80c,&header))return 0;
    uint32_t n=xk_feature_read32(header.host+0x808);
    /* Exactly n-1 backedges, no nested yields in the native vertex path. */
    if(n<2||n>256||c->preempt<(int32_t)n)return 0;
    uint32_t geom=xk_feature_read32(args.host+4),matrix=xk_feature_read32(args.host+8);
    float height,radius;memcpy(&height,args.host+12,4);memcpy(&radius,args.host+16,4);
    if(!isfinite(height)||!isfinite(radius)||(!matrix&&height<=0))return 0;
    uint32_t out=xk_feature_read32(args.host+20);
    if(!xk_feature_admit_span(arena_bytes,page_count,sp-88,112,&stack)||
       !xk_feature_admit_span(arena_bytes,page_count,out,0x4408,&output)||
       !xk_feature_admit_span(arena_bytes,page_count,c->r[7],0x80c+n*4,&reads[0])||
       !xk_feature_admit_span(arena_bytes,page_count,geom,0x60,&reads[1])||
       !xk_feature_admit_span(arena_bytes,page_count,0x1f0a68,4,&reads[2]))return 0;
    if(xk_feature_read32(reads[2].host)!=0||xk_fb_count(output.host)>256||xk_fb_count(output.host+2)>256)return 0;
    const unsigned count_off[3]={0x54,0x48,0x3c},stride[3]={16,24,12};
    uint32_t counts[3],first[3],last[3],addresses[3];
    for(unsigned i=0;i<3;i++){
        counts[i]=xk_feature_read32(reads[1].host+count_off[i]);
        addresses[i]=xk_feature_read32(reads[1].host+count_off[i]+4);
        if(!counts[i]||counts[i]>INT32_MAX)return 0;
        first[i]=UINT32_MAX;last[i]=0;
    }
    uint64_t estimated_pages=0;
    for(unsigned i=0;i<3;i++)estimated_pages+=((uint64_t)counts[i]*stride[i]+4095)/4096;
    int narrow=estimated_pages>(uint64_t)n*8;
    /* For short lists in large BSPs, bound only the referenced ranges.
     * Otherwise scanning whole mapped spans avoids repeated index walks.
     * Dependencies are validated before following any of their indices. */
    for(unsigned kind=0;kind<3;kind++){
        if(!narrow){first[kind]=0;last[kind]=counts[kind]-1;}
        else for(unsigned i=0;i<n;i++){
            uint32_t id=xk_feature_read32(reads[0].host+0x80c+i*4);
            if(kind>=1)id=xk_feature_read32(reads[3].host+(id-first[0])*16+12);
            if(kind>=2)id=xk_feature_read32(reads[4].host+(id-first[1])*24+16);
            if(id>=counts[kind])return 0;
            if(id<first[kind])first[kind]=id;
            if(id>last[kind])last[kind]=id;
        }
        uint64_t address=(uint64_t)addresses[kind]+(uint64_t)first[kind]*stride[kind];
        uint64_t bytes=((uint64_t)last[kind]-first[kind]+1)*stride[kind];
        if(address>UINT32_MAX||bytes>SIZE_MAX||
           !xk_feature_admit_span(arena_bytes,page_count,(uint32_t)address,(size_t)bytes,&reads[3+kind]))return 0;
    }
    size_t read_count=6;
    if(matrix){
        if(!xk_feature_admit_span(arena_bytes,page_count,matrix,52,&reads[6]))return 0;
        for(unsigned i=0;i<13;i++){float f;memcpy(&f,reads[6].host+i*4,4);if(!isfinite(f))return 0;}
        read_count++;
    }
    if(!xk_feature_layout_disjoint(&output,&stack,reads,read_count))return 0;
    for(unsigned i=0;i<n;i++){
        uint32_t v=xk_feature_read32(reads[0].host+0x80c+i*4);if(v>=counts[0])return 0;
        const uint8_t *point=reads[3].host+(v-first[0])*16;
        for(unsigned axis=0;axis<3;axis++){float f;memcpy(&f,point+axis*4,4);if(!isfinite(f))return 0;}
        uint32_t edge=xk_feature_read32(point+12);if(edge>=counts[1])return 0;
        if(xk_feature_read32(reads[4].host+(edge-first[1])*24+16)>=counts[2])return 0;
    }
    xk_feature_vertex_scope scope={output.host,original_emit};*result=scope;return 1;
}
static inline void xk_feature_scoped_emit(xctx *c,void *opaque){
    xk_feature_vertex_scope *scope=opaque;
    if(!xk_feature_vertex_emit_state(c,scope->output)){scope->original_emit(c);return;}
#ifdef XV_CHECK_GUEST_ADDRESS
    xv_mark_written(scope->output,0x4408);
#endif
}
static inline void xk_feature_scoped_vertex(xctx *c,void *opaque){
    xk_feature_vertex_state_scoped(c,xk_feature_point_state,xk_feature_scoped_emit,opaque);
}
/* Scope is consumed only in the vertex phase, before edge/surface callbacks
 * can yield. No static/TLS cache or pointer is retained after return. */
static inline int xk_feature_try_query_vertices(xctx *c,uint64_t arena_bytes,size_t page_count,
        xk_feature_guest_call emit,xk_feature_guest_call edge,xk_feature_guest_call surface){
    xk_feature_vertex_scope scope;
    if(!xk_feature_prepare_vertex_scope(c,arena_bytes,page_count,emit,&scope))return 0;
    xk_feature_query_state_scoped(c,xk_feature_scoped_vertex,&scope,edge,surface);return 1;
}
#endif
