#pragma once
#include "../xv_x86rt.h"
/* Startup-only experiment: immutable selection, no runtime override, counters,
 * additional scopes, locks, ownership transfer or independently retained data.
 * Feature-OFF retains the original emitted entries. Startup-OFF executes the
 * original regions, including the bounded 2D fallback block. */
#ifdef XV_NATIVE_COLLISION_TRAVERSAL
extern const unsigned xv_collision_traversal_mode;
int xv_collision_traversal_enabled(void);

static inline int xv_ct_fp_ok(void)
{
#ifdef __arm__
    unsigned fpscr;__asm__ volatile("vmrs %0, fpscr":"=r"(fpscr)::"memory");
    if(fpscr&0x00009f00u)return 0;
#endif
    return 1;
}
static inline __attribute__((always_inline)) unsigned xv_ct_word(uint32_t address)
{ unsigned word;x_guest_read(&word,address,4);return word; }
static inline double xv_ct_value(unsigned word)
{ float value;memcpy(&value,&word,4);return (double)value; }
static inline int xv_ct_finite(const unsigned *words,unsigned n)
{for(unsigned i=0;i<n;i++)if((words[i]&0x7f800000u)==0x7f800000u)return 0;return 1;}

/* Keep multiplication/addition/subtraction order explicit on ARM. In
 * particular a sign-folded VMLS is not interchangeable under every FPSCR
 * rounding mode with the original FCHS, FMUL, FADD sequence. */
#ifdef __arm__
#define XV_CT_BINARY(name,op) static inline double name(double a,double b) \
{double out;__asm__ volatile(op ".f64 %P0, %P1, %P2":"=w"(out):"w"(a),"w"(b));return out;}
XV_CT_BINARY(xv_ct_mul,"vmul")
XV_CT_BINARY(xv_ct_add,"vadd")
XV_CT_BINARY(xv_ct_sub,"vsub")
#undef XV_CT_BINARY
#else
static inline double xv_ct_mul(double a,double b){volatile double out=a*b;return out;}
static inline double xv_ct_add(double a,double b){volatile double out=a+b;return out;}
static inline double xv_ct_sub(double a,double b){volatile double out=a-b;return out;}
#endif

static inline int xv_ct_span(uint8_t *arena,const uint32_t *pages,unsigned a,unsigned n,uintptr_t *out)
{if((a&4095u)>4096u-n)return 0;*out=(uintptr_t)(arena+pages[a>>12]+(a&4095u));return 1;}
static inline int xv_ct_overlap(uintptr_t a,unsigned an,uintptr_t b,unsigned bn)
{return a<=b ? b-a<an : a-b<bn;}

/* Integer header loads retain the caller's captured roots. x87 operand reads
 * retain the runtime's global, page-splitting load semantics. There are no
 * guest writes or callbacks between these node input reads and admission. */
#pragma push_macro("X_G")
#undef X_G
#define X_G(a) ((void *)(xram_+xpt_[(uint32_t)(a)>>12]+((uint32_t)(a)&4095u)))

/* Complete 2D node decisions up to the original TEST CL at 87E68. Fallback
 * leaves guest/context/FP state untouched. Child reads/calls stay original. */
static inline __attribute__((always_inline)) int xv_ct_node2(xctx *c,uint8_t *xram_,const uint32_t *xpt_)
{
    if(!xv_ct_fp_ok())return 0;
    unsigned query=c->r[7],world=X_M32(query),index=c->r[2]*5u;
    unsigned table=X_M32(world+0x34u),node=table+index*4u;
    unsigned wy=xv_ct_word(node+4),wcy=xv_ct_word(query+0x224),
        wcx=xv_ct_word(query+0x220),wx=xv_ct_word(node),wd=xv_ct_word(node+8),
        wr=xv_ct_word(query+0x10);
    /* No guest stores or observation boundary separate the two original
     * radius reads. Avoid an input array/finite-scan loop in this short node. */
    if((wy&0x7f800000u)==0x7f800000u||(wcy&0x7f800000u)==0x7f800000u||
       (wcx&0x7f800000u)==0x7f800000u||(wx&0x7f800000u)==0x7f800000u||
       (wd&0x7f800000u)==0x7f800000u||(wr&0x7f800000u)==0x7f800000u)return 0;
    c->r[0]=world;c->r[1]=index;c->r[2]=table;c->r[6]=node;
    double y=xv_ct_mul(xv_ct_value(wy),xv_ct_value(wcy));
    double x=xv_ct_mul(xv_ct_value(wcx),xv_ct_value(wx));
    double distance=xv_ct_sub(xv_ct_add(y,x),xv_ct_value(wd));
    unsigned top=c->fsp;
    c->fsp=(top-1u)&7u;x87_compare(c,distance,xv_ct_value(wr),0);
    unsigned masked=(c->fsw>>8)&0x41u;
    unsigned left=masked!=0&&masked!=0x41u;
    double negative=-xv_ct_value(wr);
    c->fsp=(top-2u)&7u;x87_compare(c,distance,negative,0);
    c->st[(top-2u)&7u]=distance;c->st[(top-1u)&7u]=negative;c->fsp=top;
    c->r[0]=(c->r[0]&0xffff0000u)|c->fsw;
    c->r[1]=(c->r[1]&0xffffff00u)|left;
    unsigned old=c->r[3]&0xffu;
    unsigned right=!(c->fsw&0x100u);
    c->r[3]=(c->r[3]&0xffffff00u)|right;
    if(right)X_FLAGS(XK_LOGIC,0,0,0,8);
    else X_FLAGS(XK_LOGIC,old,old,0,8);
    return 1;
}

/* 3D two-way decisions consume the existing qualified plane-distance result.
 * Duplicating its capture/finite admission is more expensive and unnecessary.
 * Return original labels:
 * 1=87F94 (including its original unconditional 87F96 yield), 2=87F0F,
 * 3=87F9B. No child is loaded early and no output has been appended. */
static inline __attribute__((always_inline)) unsigned xv_ct_node3(xctx *c,uint8_t *xram_,const uint32_t *xpt_)
{
    if(!xv_ct_fp_ok())return 0;
    (void)xram_;(void)xpt_;
    x87_compare(c,X_ST(0),x87_load_f32(c,c->r[6]+0x10),0);
    unsigned masked=(c->fsw>>8)&5u;
    unsigned both=masked!=0&&masked!=5u;
    c->r[2]=(c->r[2]&0xffffff00u)|both;
    x87_compare(c,X_ST(0),x87_load_f32(c,c->r[4]+0x10),0);x87_pop(c);
    c->r[0]=(c->r[0]&0xffff0000u)|c->fsw;
    unsigned negative=(c->fsw>>8)&0x41u;X_FLAGS(XK_LOGIC,0,0,negative,8);
    if(negative)return 1;
    X_FLAGS(XK_LOGIC,0,0,both,8);c->r[0]=(c->r[0]&0xffffff00u)|1u;
    return both?3:2;
}

/* Whole projection/axis/orientation preparation, 87FE7..880DF. This region
 * writes guest scratch between later reads. Admission proves physical
 * disjointness before any FP or context/guest mutation; unusual mappings or
 * aliases execute the untouched original sequence instead. */
static __attribute__((noinline)) int xv_ct_project(xctx *c,uint8_t *xram_,const uint32_t *xpt_)
{
    if(!xv_ct_fp_ok()||g_xram!=xram_||g_xpt!=xpt_)return 0;
    unsigned q=c->r[6],sp=c->r[4],ref=c->r[3];
    unsigned plane=X_M32(c->r[7]+0x10)+((c->r[1]&0x7fffffffu)<<4);
    unsigned center=X_M32(q+0xc);
    uintptr_t stack_out,query_out;
    if(!xv_ct_span(xram_,xpt_,sp+0x14,12,&stack_out)||
       !xv_ct_span(xram_,xpt_,q+0x21c,12,&query_out)||
       xv_ct_overlap(stack_out,12,query_out,12))return 0;
    const unsigned addresses[5]={plane,center,ref,0x1eaf30,0x1f0a68};
    const unsigned sizes[5]={16,12,8,24,4};
    for(unsigned i=0;i<5;i++){
        uintptr_t input;
        if(!xv_ct_span(xram_,xpt_,addresses[i],sizes[i],&input)||
           xv_ct_overlap(input,sizes[i],stack_out,12)||
           xv_ct_overlap(input,sizes[i],query_out,12))return 0;
    }
    /* Admission established equal roots and single-page spans, so these
     * integer captures equal the original page-splitting x87 reads. */
    unsigned words[8]={X_M32(center+8),X_M32(plane+8),
        X_M32(center+4),X_M32(plane+4),X_M32(plane),
        X_M32(center),X_M32(plane+12),X_M32(0x1f0a68)};
    if(!xv_ct_finite(words,8))return 0;
    /* For normal/zero finite F32 normals, magnitude-word order gives the exact
     * original axis/tie decisions in every native rounding mode. Decline
     * subnormals (FZ-dependent comparisons) and a modified zero constant. */
    unsigned ax_word=words[4]&0x7fffffffu,ay_word=words[3]&0x7fffffffu,az_word=words[1]&0x7fffffffu;
    if((ax_word&&ax_word<0x800000u)||(ay_word&&ay_word<0x800000u)||
       (az_word&&az_word<0x800000u)||words[7])return 0;
    unsigned axis=(az_word>=ay_word&&az_word>=ax_word)?2u:(ay_word>=ax_word?1u:0u);
    unsigned normal_word=axis==0?words[4]:axis==1?words[3]:words[1];
    unsigned orientation=!(normal_word&0x80000000u)&&(normal_word&0x7fffffffu)!=0;
    unsigned lookup=(orientation+axis*2u)*4u;
    unsigned first=X_M16(0x1eaf30+lookup),second=X_M16(0x1eaf32+lookup);
    if(first>2||second>2)return 0;
    double cz=xv_ct_value(words[0]),nz=xv_ct_value(words[1]);
    double cy=xv_ct_value(words[2]),ny=xv_ct_value(words[3]);
    double nx=xv_ct_value(words[4]),cx=xv_ct_value(words[5]);
    double z=xv_ct_mul(cz,nz),y=xv_ct_mul(cy,ny),x=xv_ct_mul(nx,cx);
    double offset=-xv_ct_sub(xv_ct_add(xv_ct_add(z,y),x),xv_ct_value(words[6]));
    /* The proved single-page outputs permit direct stores with the same
     * double-to-float conversion as x87_store_f32, without a repeated generic
     * split-page check. Original write order is unchanged. */
    X_MF32(sp+0x14)=(float)xv_ct_add(xv_ct_mul(offset,nx),cx);
    X_MF32(sp+0x18)=(float)xv_ct_add(xv_ct_mul(offset,ny),cy);
    X_MF32(sp+0x1c)=(float)xv_ct_add(xv_ct_mul(offset,nz),cz);
    unsigned top=c->fsp;
    double ax=fabs(nx),ay=fabs(ny),az=fabs(nz);
    (void)ax;
    /* These quiet finite comparisons cannot raise an FP exception. Their
     * intermediate native condition bits are overwritten by the unchanged
     * orientation comparison below. Reconstruct guest status, including the
     * emitter's existing OR of TOP bits, rather than correcting its model. */
    unsigned status=(c->fsw&~0x4700u)|(((top-3u)&7u)<<11);
    if(axis==2)status|=az_word==ax_word?0x4000u:0;
    else status|=(((top-2u)&7u)<<11)|(ay_word<ax_word?0x100u:ay_word==ax_word?0x4000u:0);
    c->fsw=(uint16_t)status;
    c->st[(top-3u)&7u]=az;c->st[(top-2u)&7u]=ay;
    X_M16(q+0x21c)=(uint16_t)axis;
    c->fsp=(top-1u)&7u;x87_compare(c,axis==0?nx:axis==1?ny:nz,xv_ct_value(words[7]),0);
    /* Preserve the emitted flags, including its SBB input carry. The final
     * shift overwrites all lazy fields, but both shifts' stored CF/OF values
     * are covered by the complete-context oracle. */
    X_M8(q+0x21e)=(uint8_t)orientation;
    unsigned table=x_shl32(c,orientation+axis*2u,2);
    (void)table;
    double projected=xv_ct_value(X_M32(sp+second*4+0x14));
    unsigned raw=X_M32(sp+first*4+0x14);
    X_MF32(q+0x224)=(float)projected;X_M32(q+0x220)=raw;
    c->st[(top-1u)&7u]=projected;c->fsp=top;
    c->r[0]=first;c->r[1]=q;c->r[2]=X_M32(ref+4);
    return 1;
}
#pragma pop_macro("X_G")
#endif
