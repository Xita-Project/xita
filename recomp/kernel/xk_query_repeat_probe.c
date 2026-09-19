/* Input-only diagnostic for the selected world collision-query wrapper. */
#include "xk.h"
#include "xk_query_repeat.h"
#include <string.h>
#include <limits.h>
extern unsigned xv_object_world_run_admit(xctx *);
extern void xv_object_math_report_check(void);
extern unsigned xk_mem_arena_size(void);
extern int xv_watch_n,xv_trace_funcs;
static XvQueryRepeat probe;
static unsigned ready;
static uintptr_t roots[3];

/* Never invoke guest fault/MMIO/guard policy from an observer. Allow byte
 * alignment and page crossings: the breakable filter deliberately starts +1. */
static int probe_copy(uint32_t address,void *out,unsigned bytes)
{
    if(!g_xram||!g_xpt||!bytes||address>UINT32_MAX-(bytes-1u))return 0;
    unsigned size=xk_mem_arena_size();unsigned char *dst=out;
    if(size<4096u)return 0; /* arena size includes its trailing trash page */
    while(bytes) {
        unsigned base=g_xpt[address>>12],offset=address&4095u;
        unsigned n=4096u-offset;if(n>bytes)n=bytes;
        if((base&4095u)||base>=size-4096u||base>size||offset>size-base||n>size-base-offset)return 0;
        memcpy(dst,g_xram+base+offset,n);dst+=n;bytes-=n;
        if(bytes)address+=n;
    }
    return 1;
}
void xv_query_repeat_probe_observe(xctx *c)
{
    /* Predicate checks actual native worker, exact context, active job and
     * held actor guard. Service threads borrowing c must not enter. */
    if(!xv_object_world_run_admit(c))return;
    if(!ready){xv_query_repeat_init(&probe);ready=1;}
    uintptr_t now[3]={(uintptr_t)g_xram,(uintptr_t)g_xpt,(uintptr_t)g_img_base};
    if(memcmp(now,roots,sizeof now)){
        xv_query_repeat_invalidate(&probe);memcpy(roots,now,sizeof roots);
    }
    XvQueryRepeatKey key={0};uint32_t args[2];unsigned char filter[32];
    if(xv_watch_n||xv_trace_funcs||c->r[4]>UINT32_MAX-8u||
       !probe_copy(c->r[4]+4u,args,sizeof args))goto invalid;
    key.arena=now[0];key.pages=now[1];key.image=now[2];
    key.bsp=c->r[0];key.filter_bits=(uint16_t)c->r[1];key.filter=c->r[2];
    key.point=args[0];key.radius=args[1];key.fcw=c->fcw;
#if defined(__arm__)
    __asm__ volatile("vmrs %0,fpscr":"=r"(key.fp_control)::"memory");
    key.fp_control&=XV_QUERY_REPEAT_FP_CONTROL_MASK;
#endif
    if(!probe_copy(key.point,key.center,sizeof key.center)||
       !probe_copy(key.bsp,key.geometry,sizeof key.geometry)||
       !probe_copy(0x1f0a68u,&key.zero,sizeof key.zero)||
       !probe_copy(0x1eaf30u,key.selector,sizeof key.selector))goto invalid;
    int valid_filter=key.filter_bits==256u&&probe_copy(key.filter,filter,sizeof filter);
    xv_query_repeat_observe(&probe,&key,valid_filter?filter:NULL);return;
invalid:
    xv_query_repeat_observe(&probe,NULL,NULL);
}
void xv_query_repeat_probe_epoch(void)
{
    xv_object_math_report_check();
    if(ready)xv_query_repeat_advance_epoch(&probe);
}
void xv_query_repeat_probe_report(unsigned frames)
{
    xv_object_math_report_check();
    if(!ready||!frames)return;
    XvQueryRepeatCounts n;xv_query_repeat_take(&probe,&n);
#define Q(v) (unsigned long long)(v)
    XK_LOG("[query-repeat] %u frames calls/valid/invalid %llu/%llu/%llu filter-valid/unavailable %llu/%llu; input within/prior/miss %llu/%llu/%llu filter within/prior/miss %llu/%llu/%llu; passes %llu evictions %llu invalidations %llu; last64 inputs only, not reusable results\n",frames,
        Q(n.calls),Q(n.valid),Q(n.invalid),Q(n.filter_valid),Q(n.filter_unavailable),
        Q(n.input.within_epoch),Q(n.input.prior_epoch_only),Q(n.input.misses),
        Q(n.filter.within_epoch),Q(n.filter.prior_epoch_only),Q(n.filter.misses),
        Q(n.epochs),Q(n.evictions),Q(n.invalidations));
#undef Q
}
