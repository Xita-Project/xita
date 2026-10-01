/* State-preserving outer query driver. Experimental, no runtime hook.
 * Child callbacks must preserve their complete original guest contracts.
 * Kept separate from output-only native geometry until child boundaries pass. */
#ifndef XK_FEATURE_STATE_H
#define XK_FEATURE_STATE_H
#include "xv_x86rt.h"
#include "xk_feature_build.h"
typedef void (*xk_feature_guest_call)(xctx *);
typedef void (*xk_feature_scoped_call)(xctx *,void *);
static inline void xk_feature_call_plain(xctx *c,void *opaque){
    xk_feature_guest_call *fn=opaque;(*fn)(c);
}
static inline void xk_feature_inc_state(xctx *c,unsigned reg){
    uint32_t carry=XF_C(c);c->f_cf_override=1;c->f_cf=carry;c->r[reg]++;
}
static inline void xk_feature_cmp_state(xctx *c,uint32_t a,uint32_t b){
    X_FLAGS(XK_SUB,a,b,a-b,32);
}
/* Native point transform with the observable retired x87 slots reconstructed.
 * Keep FP contraction off. Reads/stores retain axis order (including aliases).
 * This models the current lifted double arithmetic, not arbitrary x87 modes. */
static inline void xk_feature_point_state(xctx *c){
    uint32_t matrix=c->r[1],output=c->r[0],top=c->fsp;
    double x=x87_load_f32(c,c->r[2]);
    double y=x87_load_f32(c,c->r[2]+4);
    double z=x87_load_f32(c,c->r[2]+8);
    c->r[2]=X_M32(matrix);xk_feature_cmp_state(c,c->r[2],0x3f800000);
    if(!XF_Z(c)){
        x*=x87_load_f32(c,matrix);y*=x87_load_f32(c,matrix);z*=x87_load_f32(c,matrix);
    }
    double result=0,xterm=0;
    for(unsigned axis=0;axis<2;axis++){
        double zterm=z*x87_load_f32(c,matrix+28+axis*4);
        double yterm=y*x87_load_f32(c,matrix+16+axis*4);
        result=zterm+yterm;
        xterm=x*x87_load_f32(c,matrix+4+axis*4);
        result+=xterm;result+=x87_load_f32(c,matrix+40+axis*4);
        x87_store_f32(c,output+axis*4,result);
    }
    c->st[(top-4)&7]=result;c->st[(top-5)&7]=xterm;
    double zterm=z*x87_load_f32(c,matrix+36);
    double yterm=y*x87_load_f32(c,matrix+24);
    result=zterm+yterm;xterm=x*x87_load_f32(c,matrix+12);
    result+=xterm;result+=x87_load_f32(c,matrix+48);
    x87_store_f32(c,output+8,result);
    c->st[(top-1)&7]=result;c->st[(top-2)&7]=xterm;c->st[(top-3)&7]=yterm;
    c->r[4]+=4;
}
/* Experimental admitted emitter, NOT a guest entry hook. Caller must prove
 * output spans 0x4408 contiguous writable bytes and is physically disjoint
 * from point, argument/scratch stack and constants for the entire call.
 * The owned-image zero constant at 0x1f0a68 must remain unchanged.
 * Native stores require write-stamp integration before checked runtime use. */
static inline int xk_feature_vertex_emit_state(xctx *c,uint8_t *output){
    uint32_t base=c->r[1],sp=c->r[4];
    unsigned ns=xk_fb_count(output),nc=xk_fb_count(output+2);
    if(ns>256||nc>256)return 0;
    float p[3],height,radius;
    x_guest_read(p,c->r[6],12);x_guest_read(&height,sp+4,4);x_guest_read(&radius,sp+8,4);
    if(!isfinite(height)||!isfinite(radius)||!isfinite(p[0])||!isfinite(p[1])||!isfinite(p[2]))return 0;
    uint32_t radius_bits=X_M32(sp+8),height_bits=X_M32(sp+4);
    xk_feature_metadata m={X_M32(sp+12),X_M32(sp+16),X_M8(sp+20),X_M8(sp+24),X_R16(7)};
    /* Admission makes the output computation independent of stack/state writes. */
    if(!xk_feature_build_vertex(output,&m,p,height,radius))return 0;
    X_PUSH32(c->r[3]);X_PUSH32(c->r[5]);
    c->r[2]=ns;
    if(ns<256){c->r[0]=base+8+ns*28;c->r[2]=radius_bits;ns++;}
    x87_push(c,(double)height);x87_compare(c,X_ST(0),x87_load_f32(c,0x1f0a68),0);x87_pop(c);
    X_R16(0)=c->fsw;
    X_FLAGS(XK_LOGIC,0,0,X_R8H(0)&0x41,8);
    if(XF_Z(c)){
        x87_push(c,(double)p[2]-(double)height);
        if(ns<256){
            c->r[0]=base+8+ns*28;
            x87_push(c,(double)p[1]);x87_pop(c);
        }
        c->r[2]=nc;
        X_FLAGS(XK_SUB,nc,256,(uint16_t)(nc-256),16);
        if(nc<256){
            xk_feature_inc_state(c,2);
            c->r[0]=base+0x1c08+nc*40;
            c->r[2]=height_bits;c->r[1]=radius_bits;
            x87_push(c,(double)p[1]);x87_pop(c);
        }
        x87_pop(c);
    }
    c->r[5]=X_POP32();c->r[3]=X_POP32();c->r[4]+=28;
    return 1;
}
/* Vertex boundary: preserve stack scratch, partial-register writes and the
 * transform's FP state. The numerical children remain replaceable callbacks. */
static inline void xk_feature_vertex_state_scoped(xctx *c,xk_feature_guest_call transform,
        xk_feature_scoped_call emit,void *opaque){
    c->r[4]-=12;X_PUSH32(c->r[6]);c->r[6]=c->r[2];
    c->r[0]=x_shl32(c,c->r[0],4);X_PUSH32(c->r[7]);
    c->r[0]+=X_M32(c->r[6]+0x58);
    xk_feature_cmp_state(c,c->r[3],UINT32_MAX);
    c->r[7]=X_M32(c->r[6]+0x4c);c->r[6]=X_M32(c->r[6]+0x40);
    c->r[2]=c->r[0];c->r[0]=X_M32(c->r[2]+12);
    c->r[0]=X_M32(c->r[7]+c->r[0]*24+16);
    c->r[7]=c->r[6]+c->r[0]*12;
    if(XF_Z(c))c->r[6]=c->r[0];
    else{
        X_FLAGS(XK_LOGIC,c->r[6],UINT32_MAX,UINT32_MAX,32);
        c->r[6]=UINT32_MAX;
    }
    X_FLAGS(XK_LOGIC,0,0,c->r[1],32);
    if(!XF_Z(c)){
        c->r[0]=c->r[4]+8;X_PUSH32(0x86480);transform(c);
    }else c->r[0]=c->r[2];
    c->r[1]=X_M8(c->r[7]+9);c->r[2]=X_M8(c->r[7]+8);
    X_R16(7)=X_M16(c->r[7]+10);
    X_PUSH32(c->r[1]);c->r[1]=X_M32(c->r[4]+0x20);
    X_PUSH32(c->r[2]);c->r[2]=X_M32(c->r[4]+0x20);
    X_PUSH32(c->r[6]);X_PUSH32(c->r[3]);X_PUSH32(c->r[1]);
    c->r[1]=X_M32(c->r[4]+0x34);X_PUSH32(c->r[2]);c->r[6]=c->r[0];
    X_PUSH32(0x864ab);emit(c,opaque);
    c->r[7]=X_POP32();c->r[6]=X_POP32();c->r[4]+=28;
}
static inline void xk_feature_vertex_state(xctx *c,xk_feature_guest_call transform,
        xk_feature_guest_call emit){
    xk_feature_vertex_state_scoped(c,transform,xk_feature_call_plain,&emit);
}
/* Every phase receives an explicit caller-owned scope. Callbacks may yield:
 * a scope is not a lifetime guarantee and must not cache mappings across a
 * yield without independent ownership. No process-global or TLS query cache. */
static inline void xk_feature_query_state_all_scoped(xctx *c,
        xk_feature_scoped_call vertex,xk_feature_scoped_call edge,
        xk_feature_scoped_call surface,void *opaque){
    X_PUSH32(c->r[1]);X_PUSH32(c->r[3]);c->r[3]=c->r[0];
    c->r[0]=X_M32(c->r[7]+0x808);
    X_FLAGS(XK_LOGIC,0,0,c->r[0],32);
    X_PUSH32(c->r[5]);c->r[5]=X_M32(c->r[4]+0x20);
    X_PUSH32(c->r[6]);X_W32(c->r[4]+0xc)=0;
    if(!(XF_Z(c)||(XF_S(c)!=XF_O(c)))){
        c->r[0]=c->r[7]+0x80c;X_W32(c->r[4]+0x24)=c->r[0];
        for(;;){
            c->r[1]=X_M32(c->r[4]+0x20);c->r[2]=X_M32(c->r[4]+0x1c);
            c->r[0]=X_M32(c->r[4]+0x24);c->r[0]=X_M32(c->r[0]);
            X_PUSH32(c->r[5]);X_PUSH32(c->r[1]);c->r[1]=X_M32(c->r[4]+0x20);
            X_PUSH32(c->r[2]);c->r[2]=X_M32(c->r[4]+0x20);
            X_PUSH32(0x8693e);vertex(c,opaque);
            c->r[0]=X_M32(c->r[4]+0xc);c->r[2]=X_M32(c->r[4]+0x24);
            c->r[1]=X_M32(c->r[7]+0x808);xk_feature_inc_state(c,0);c->r[2]+=4;
            xk_feature_cmp_state(c,c->r[0],c->r[1]);
            X_W32(c->r[4]+0xc)=c->r[0];X_W32(c->r[4]+0x24)=c->r[2];
            if(XF_S(c)==XF_O(c))break;
            X_PREEMPT();
        }
    }
    c->r[0]=X_M32(c->r[7]+0x404);c->r[6]=0;
    X_FLAGS(XK_LOGIC,0,0,c->r[0],32);
    if(!(XF_Z(c)||(XF_S(c)!=XF_O(c)))){
        c->r[1]=c->r[7]+0x408;X_W32(c->r[4]+0x24)=c->r[1];
        for(;;){
            c->r[2]=X_M32(c->r[4]+0x20);c->r[0]=X_M32(c->r[4]+0x1c);
            c->r[1]=X_M32(c->r[4]+0x18);
            X_PUSH32(c->r[5]);X_PUSH32(c->r[3]);X_PUSH32(c->r[2]);
            c->r[2]=X_M32(c->r[4]+0x30);X_PUSH32(c->r[0]);c->r[0]=X_M32(c->r[2]);
            X_PUSH32(c->r[1]);c->r[1]=X_M32(c->r[4]+0x28);
            X_PUSH32(0x86992);edge(c,opaque);
            c->r[1]=X_M32(c->r[4]+0x24);c->r[0]=X_M32(c->r[7]+0x404);
            xk_feature_inc_state(c,6);c->r[1]+=4;xk_feature_cmp_state(c,c->r[6],c->r[0]);
            X_W32(c->r[4]+0x24)=c->r[1];
            if(XF_S(c)==XF_O(c))break;
            X_PREEMPT();
        }
    }
    xk_feature_cmp_state(c,X_M32(c->r[7]),0);X_W32(c->r[4]+0xc)=0;
    if(!(XF_Z(c)||(XF_S(c)!=XF_O(c)))){
        c->r[0]=c->r[7]+4;X_W32(c->r[4]+0x24)=c->r[0];
        for(;;){
            c->r[1]=X_M32(c->r[4]+0x20);c->r[2]=X_M32(c->r[4]+0x1c);
            c->r[0]=X_M32(c->r[4]+0x24);c->r[6]=X_M32(c->r[4]+0x18);
            X_PUSH32(c->r[5]);X_PUSH32(c->r[3]);X_PUSH32(c->r[1]);
            c->r[1]=X_M32(c->r[0]);c->r[0]=X_M32(c->r[4]+0x20);
            X_PUSH32(c->r[2]);X_PUSH32(c->r[1]);X_PUSH32(0x869e0);surface(c,opaque);
            c->r[0]=X_M32(c->r[4]+0xc);c->r[2]=X_M32(c->r[4]+0x24);
            c->r[1]=X_M32(c->r[7]);xk_feature_inc_state(c,0);c->r[2]+=4;
            xk_feature_cmp_state(c,c->r[0],c->r[1]);
            X_W32(c->r[4]+0xc)=c->r[0];X_W32(c->r[4]+0x24)=c->r[2];
            if(XF_S(c)==XF_O(c))break;
            X_PREEMPT();
        }
    }
    c->r[6]=X_POP32();c->r[5]=X_POP32();c->r[3]=X_POP32();c->r[1]=X_POP32();
    c->r[4]+=24;
}
typedef struct {
    xk_feature_scoped_call vertex;
    void *vertex_scope;
    xk_feature_guest_call edge,surface;
} xk_feature_query_legacy_scope;
static inline void xk_feature_query_legacy_vertex(xctx *c,void *opaque){
    xk_feature_query_legacy_scope *s=opaque;s->vertex(c,s->vertex_scope);
}
static inline void xk_feature_query_legacy_edge(xctx *c,void *opaque){
    xk_feature_query_legacy_scope *s=opaque;s->edge(c);
}
static inline void xk_feature_query_legacy_surface(xctx *c,void *opaque){
    xk_feature_query_legacy_scope *s=opaque;s->surface(c);
}
static inline void xk_feature_query_state_scoped(xctx *c,xk_feature_scoped_call vertex,
        void *opaque,xk_feature_guest_call edge,xk_feature_guest_call surface){
    xk_feature_query_legacy_scope scope={vertex,opaque,edge,surface};
    xk_feature_query_state_all_scoped(c,xk_feature_query_legacy_vertex,
        xk_feature_query_legacy_edge,xk_feature_query_legacy_surface,&scope);
}
static inline void xk_feature_query_state(xctx *c,xk_feature_guest_call vertex,
        xk_feature_guest_call edge,xk_feature_guest_call surface){
    xk_feature_query_state_scoped(c,xk_feature_call_plain,&vertex,edge,surface);
}
#endif
