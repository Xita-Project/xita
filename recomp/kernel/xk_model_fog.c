/* Owner-only exact-input memo for 70110:70A42..70D07. The generated caller
 * always retains the original cold body. No allocation, worker, or GPU work. */
#if defined(XV_MODEL_FOG) && XV_MODEL_FOG
#include "xk.h"
#include "xk_owner_phase.h"
#include "xk_model_fog.h"
#include <stdlib.h>
extern int xv_watch_n __attribute__((weak)),xv_trace_funcs __attribute__((weak));

static const uint32_t addresses[18] = {
    0x1f0a68,0x1f0a78,0x2fc6c8,0x2fc6cc,0x2fc6d0,0x2fc8ac,
    0x2fc8b0,0x2fc8b4,0x2fc8b8,0x2fc8bc,0x2fc8c0,0x2fc8c8,
    0x2fc8cc,0x2fc8d0,0x2fc8d4,0x2fc8d8,0x2fc8dc,0x2fc8e0
};
typedef struct {
    uint32_t value[19],flag,sp,top,fsw,fcw,fpscr,stackpage,arena,lo,hi,generation;
    uintptr_t ram,pt,image;
} FogKey;
_Static_assert(sizeof(FogKey)==30*sizeof(uint32_t)+3*sizeof(uintptr_t), "fog key must have no padding");
typedef struct {
    FogKey key;
    uint32_t regs[8],flags[9],stack[20],fpscr;
    double st[5];
    uint16_t fsw;
    unsigned valid;
} FogCache;
static FogCache last;
static FogKey pending;
static unsigned pending_valid;
static unsigned hits,misses,declines,disabled,invalidations;
static int configured,enabled,override = -1;

static unsigned fpscr_get(void)
{
#if defined(__arm__)
    unsigned value;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(value) :: "memory");
    return value;
#else
    return ~0u; /* This replay contract has only been qualified on ARM. */
#endif
}
static void fpscr_set(unsigned value)
{
#if defined(__arm__)
    __asm__ volatile("vmsr fpscr, %0" :: "r"(value) : "memory");
#else
    (void)value;
#endif
}
static void invalidate(void)
{
    if(last.valid || pending_valid) invalidations++;
    last.valid=pending_valid=0;
}
void xk_model_fog_override(xctx *c,int value)
{
    uint32_t generation=0;
    if(xv_owner_phase_active(c,XV_OWNER_SCENE,&generation)<0)return;
    invalidate();override=value<0?-1:value!=0;
}
static int admit(xctx *c,FogKey *key,uint32_t generation,
                 const uint8_t *ram,const uint32_t *pt,const uint8_t *image)
{
    uint32_t sp=c->r[4],fp=fpscr_get();
    if(ram!=g_xram || pt!=g_xpt || image!=g_img_base || !ram || !pt || !image ||
       (c->fcw!=0x023f && c->fcw!=0x027f) || c->fsp>7 || (fp&~0xf300009fu) ||
       sp<36 || sp>UINT32_MAX-0xabu || (sp&3) ||
       ((sp-36)>>12)!=((sp+0xab)>>12))return 0;
    uint32_t arena=xk_mem_arena_size(),lo=xk_mem_image_lo(),hi=xk_mem_image_hi();
    /* The final arena page is the unmapped/trash page. Validate all spans
     * before reading any guest data, including the direct image flag. */
    if(arena<8192 || lo>0x2fc8a8 || hi<=0x2fc8a8)return 0;
    uint32_t page=pt[sp>>12],a=pt[0x1f0],b=pt[0x2fc];
    if(((page|a|b)&4095) || page>=arena-4096 || a>=arena-4096 || b>=arena-4096 ||
       page==a || page==b)return 0;
    uintptr_t base=(uintptr_t)ram,flag=(uintptr_t)image+0x2fc8a8u;
    if(flag<base || flag-base>=arena-4096 ||
       (flag>=base+page && flag<base+page+4096))return 0;
    key->value[0]=X_M32(sp+0x48);
    for(unsigned i=0;i<18;i++)key->value[i+1]=X_M32(addresses[i]);
    for(unsigned i=0;i<19;i++){
        unsigned v=key->value[i]&0x7fffffffu;
        if(v>0x47800000u || (v && v<0x00800000u))return 0;
    }
    if(!(key->value[11]&0x7fffffffu) || key->value[11]==key->value[10])return 0;
    key->flag=X_IMG8(0x2fc8a8);key->sp=sp;key->top=c->fsp;
    key->fsw=c->fsw;key->fcw=c->fcw;
    /* Arithmetic overwrites NZCV; every sticky/control bit remains keyed. */
    key->fpscr=fp&~0xf0000000u;
    key->stackpage=page;key->arena=arena;key->lo=lo;key->hi=hi;
    key->generation=generation;key->ram=(uintptr_t)ram;
    key->pt=(uintptr_t)pt;key->image=(uintptr_t)image;
    return 1;
}
int xk_model_fog_begin(xctx *c,const uint8_t *ram,const uint32_t *pt,
                       const uint8_t *image,unsigned *token)
{
    *token=0;
    /* This query rejects native workers before accessing owner scheduler
     * state. No mutable cache/config/counter is touched by foreign callers. */
    uint32_t generation=0;
    if(xv_owner_phase_active(c,XV_OWNER_SCENE,&generation)<0)return 0;
    if((&xv_watch_n && xv_watch_n) || (&xv_trace_funcs && xv_trace_funcs)
#ifdef XV_CHECK_GUEST_ADDRESS
       || xv_watch_len
#endif
    ){invalidate();declines++;return 0;}
    if(!configured){const char *e=getenv("XV_MODEL_FOG");enabled=!e||atoi(e)!=0;configured=1;}
    if(!(override<0?enabled:override)){invalidate();disabled++;return 0;}
    FogKey key;
    if(!admit(c,&key,generation,ram,pt,image)){invalidate();declines++;return 0;}
    if(last.valid && !memcmp(&last.key,&key,sizeof key)){
        unsigned ebx=c->r[3],sp=c->r[4];hits++;
        for(unsigned i=0;i<8;i++)if(i!=4 && i!=5)c->r[i]=last.regs[i];
        memcpy(&c->f_kind,last.flags,sizeof last.flags);
        for(unsigned i=0;i<5;i++)c->st[(c->fsp-1-i)&7]=last.st[i];
        c->fsw=last.fsw;
        /* EBX saved by the first child belongs to this invocation. */
        for(unsigned i=0;i<9;i++)X_W32(sp-36+i*4)=i?last.stack[i]:ebx;
        for(unsigned i=0;i<8;i++)X_W32(sp+0x10+i*4)=last.stack[9+i];
        for(unsigned i=0;i<3;i++)X_W32(sp+0xa0+i*4)=last.stack[17+i];
        fpscr_set(last.fpscr);return 1;
    }
    misses++;last.valid=0;pending=key;pending_valid=1;*token=generation;
    return 0;
}
void xk_model_fog_end(xctx *c,unsigned token)
{
    if(!token)return;
    unsigned fp=fpscr_get();
    uint32_t generation=token;
    if(xv_owner_phase_active(c,XV_OWNER_SCENE,&generation)<0)return;
    if(!pending_valid || generation!=pending.generation)return;
    pending_valid=0;
    if((uintptr_t)g_xram!=pending.ram || (uintptr_t)g_xpt!=pending.pt ||
       (uintptr_t)g_img_base!=pending.image || c->r[4]!=pending.sp ||
       c->fsp!=pending.top || c->fcw!=pending.fcw ||
       xk_mem_arena_size()!=pending.arena || xk_mem_image_lo()!=pending.lo ||
       xk_mem_image_hi()!=pending.hi || g_xpt[pending.sp>>12]!=pending.stackpage){
        invalidate();return;
    }
    last.fpscr=fp;last.key=pending;
    memcpy(last.regs,c->r,sizeof last.regs);memcpy(last.flags,&c->f_kind,sizeof last.flags);
    for(unsigned i=0;i<5;i++)last.st[i]=c->st[(c->fsp-1-i)&7];
    last.fsw=c->fsw;
    unsigned sp=c->r[4];
    for(unsigned i=0;i<9;i++)last.stack[i]=X_M32(sp-36+i*4);
    for(unsigned i=0;i<8;i++)last.stack[9+i]=X_M32(sp+0x10+i*4);
    for(unsigned i=0;i<3;i++)last.stack[17+i]=X_M32(sp+0xa0+i*4);
    last.valid=1;
}
void xk_model_fog_report(unsigned frames)
{
    /* Called only after xk_owner_phase_report has validated its live owner. */
    XK_LOG("[model-fog] %u frames hits %u misses %u declined %u disabled %u invalidations %u; exact-input arithmetic only\n",
           frames,hits,misses,declines,disabled,invalidations);
    hits=misses=declines=disabled=invalidations=0;
}
#endif
