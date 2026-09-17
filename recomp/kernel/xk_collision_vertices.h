#pragma once
#include "../xv_x86rt.h"
#ifdef XV_NATIVE_COLLISION_VERTICES
#include <stdlib.h>
void xv_collision_vertices_init(void);
int xv_collision_vertices_available(void);
int xv_collision_vertices_enabled(void);
/* Nonfatal scope/owner readiness, including scopes on suspended guest fibers.
 * The caller must separately drain object work and not yield before control. */
int xv_collision_vertices_control_ready(void);
void xv_collision_vertices_override(int enabled);
unsigned xv_collision_vertices_calls(void);
/* Bit zero selects the experiment. Upper bits retain active scopes across
 * cooperative yields. Control changes require the bound, drained owner. */
extern unsigned xv_collision_vertices_state, xv_collision_vertices_count;
static inline int xv_collision_vertices_fp_ok(void)
{
#if defined(__arm__)
    unsigned fpscr;__asm__ volatile("vmrs %0, fpscr":"=r"(fpscr)::"memory");
    return !(fpscr&0x00009f00u);
#else
    return 1;
#endif
}
static inline __attribute__((always_inline)) unsigned xv_collision_vertices_begin(void)
{
    unsigned old=__atomic_load_n(&xv_collision_vertices_state,__ATOMIC_ACQUIRE);
    if((old&1u)&&!xv_collision_vertices_fp_ok())return 0;
    while(old&1u){
        if(old>~0u-2u)abort();
        if(__atomic_compare_exchange_n(&xv_collision_vertices_state,&old,old+2u,1,
                                      __ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){
            __atomic_fetch_add(&xv_collision_vertices_count,1,__ATOMIC_RELAXED);
            return 1;
        }
    }
    return 0;
}
static inline void xv_collision_vertices_end(unsigned *token)
{
    if(*token){
        if((__atomic_fetch_sub(&xv_collision_vertices_state,2,__ATOMIC_RELEASE)>>1)==0)abort();
        *token=0;
    }
}

/* Experimental complete vertex pass at 86F9B..8709A. No allocation, shared
 * host scratch, lock change or omitted guest stack writes. The original owns
 * its query/world memory; this helper introduces no additional concurrency.
 * Each taken original backedge publishes context before X_PREEMPT and resumes
 * its already-selected target, even if the callback changes registers/maps. */
/* 0: complete pass, 1: resume original 86FBA, 2: resume original 87050. */
#pragma push_macro("X_G")
#undef X_G
#define X_G(a) ((void *)(cv_arena+cv_pages[(uint32_t)(a)>>12]+((uint32_t)(a)&4095u)))
static inline __attribute__((always_inline)) unsigned xv_collision_vertices(xctx *c,uint8_t *cv_arena,const uint32_t *cv_pages)
{
    uint32_t a=c->r[0],b=c->r[3],d=c->r[2],q=c->r[6],p=c->r[5],
             e=c->r[7],x=c->r[1],sp=c->r[4];
#define CV_SAVE() do { c->r[0]=a;c->r[1]=x;c->r[2]=d;c->r[3]=b; \
    c->r[4]=sp;c->r[5]=p;c->r[6]=q;c->r[7]=e; } while(0)
#define CV_LOAD() do { a=c->r[0];x=c->r[1];d=c->r[2];b=c->r[3]; \
    sp=c->r[4];p=c->r[5];q=c->r[6];e=c->r[7]; } while(0)
    x87_push(c,x87_load_f32(c,q+0x10));
    a=X_M32(p+4);
    x87_push(c,X_ST(0));X_ST(0)=X_ST(0)*X_ST(1);
    x=sp+0x14;X_M8(sp+0x13)=0;X_M32(sp+0x30)=x;
    x87_store_f32(c,sp+0x34,X_ST(0));x87_pop(c);x87_pop(c);
vertex:
    x=X_M32(q);e=X_M32(x+0x4c);a*=3;e+=a*8;
    /* CMP/SETE modifies only AL, then MOVZX consumes it. The higher EAX bits
     * are overwritten before any observation, while EBX retains 0/1. */
    b=X_M32(e+0x14)==d;
    d=X_M32(e+b*4);a=x_shl32(c,d,4);a+=X_M32(x+0x58);
    x=X_M32(q+0x0c);X_M32(sp+0x38)=b;X_M32(sp+0x2c)=a;
    X_M32(sp+0x28)=x;a=X_M32(sp+0x30);x=X_M32(sp+0x2c);
    c->xmm[0][0]=X_MF32(x);c->xmm[0][1]=0;
    c->xmm[0][2]=X_MF32(x+4);c->xmm[0][3]=X_MF32(x+8);
    x=X_M32(sp+0x28);
    c->xmm[1][0]=X_MF32(x);c->xmm[1][1]=0;
    c->xmm[1][2]=X_MF32(x+4);c->xmm[1][3]=X_MF32(x+8);
    for(unsigned i=0;i<4;i++)c->xmm[0][i]-=c->xmm[1][i];
    for(unsigned i=0;i<4;i++)c->xmm[0][i]*=c->xmm[0][i];
    c->xmm[2][0]=c->xmm[0][0];
    x_shufps(c,c->xmm[0],c->xmm[0],14);
    c->xmm[2][0]+=c->xmm[0][0];
    x_shufps(c,c->xmm[0],c->xmm[0],57);
    c->xmm[2][0]+=c->xmm[0][0];X_MF32(a)=c->xmm[2][0];
    q=X_M32(sp+0x40);
    x87_push(c,x87_load_f32(c,sp+0x14));
    x87_compare(c,X_ST(0),x87_load_f32(c,sp+0x34),0);x87_pop(c);
    a=(a&0xffff0000u)|c->fsw;
    X_FLAGS(XK_LOGIC,0,0,(a>>8)&0x41,8);
    if(!XF_P(c)){
        a=X_M32(q+0x14);p=X_M32(a+0x808);b=0;
        X_FLAGS(XK_LOGIC,0,0,p,32);
        if((int32_t)p>0){
            x=0;
            int32_t budget=c->preempt;
            uint32_t carry=c->f_cf;
scan:
            { uint32_t value=X_M32(a+x*4+0x80c);
              if(value==d){
                  c->preempt=budget;c->f_cf=carry;
                  X_FLAGS(XK_SUB,value,d,value-d,32);goto found;
              }
              carry=value<d; }
            b++;x=(uint32_t)(int32_t)(int16_t)b;
            { uint32_t count=X_M32(a+0x808);
              if((int32_t)x<(int32_t)count){
                  /* Only a taken backedge consumes budget. Ordinary reads
                   * continue in their original order; no guest write/call or
                   * observation occurs before this original yield point. */
                  if(--budget<=0){
                      CV_SAVE();c->preempt=budget;c->f_cf=carry;
                      X_FLAGS(XK_SUB,x,count,x-count,32);
                      xv_preempt(c);
                      if(!xv_collision_vertices_fp_ok())return 2;
                      CV_LOAD();budget=c->preempt;carry=c->f_cf;
                  }
                  goto scan;
              } }
            c->preempt=budget;c->f_cf=carry;
        }
        X_FLAGS(XK_SUB,p,0x100,p-0x100,32);
        if((int32_t)p<0x100){
            X_M32(a+p*4+0x80c)=d;
            c->f_cf=XF_C(c);c->f_cf_override=1;
            X_M32(a+0x808)=X_M32(a+0x808)+1;
            q=X_M32(sp+0x40);
        }
found:
        b=X_M32(sp+0x38);p=X_M32(sp+0x20);X_M8(sp+0x13)=1;
    }
    a=X_M32(e+b*4+8);x=X_M32(p+4);
    X_FLAGS(XK_SUB,a,x,a-x,32);
    if(a!=x){
        CV_SAVE();
        if(--c->preempt<=0){
            xv_preempt(c);if(!xv_collision_vertices_fp_ok())return 1;
        }
        CV_LOAD();d=X_M32(sp+0x44);goto vertex;
    }
    CV_SAVE();
#undef CV_SAVE
#undef CV_LOAD
    return 0;
}
#pragma pop_macro("X_G")
#endif
