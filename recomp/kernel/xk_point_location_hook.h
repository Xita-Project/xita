/* Included in the one CE shard containing 17A8B0. Default off.
 * Verification replays without intermediate yields, retains the guest result,
 * then applies the guest's backedge budget. Never use verifier FPS as speed data. */
#pragma once
#include <stdlib.h>
#if !defined(__arm__)
#include <fenv.h>
#endif
#include "xk_point_location.h"
extern int xv_object_is_worker_thread(void) __attribute__((weak));
extern void xk_os_log(const char *, ...);
static int pl_mode = -1;
static unsigned pl_verified, pl_mismatch, pl_declined, pl_native;
#if defined(__arm__)
typedef uint32_t pl_fp_state;
static pl_fp_state pl_fp_get(void) { uint32_t v;__asm__ volatile("vmrs %0, fpscr":"=r"(v));return v; }
static void pl_fp_set(pl_fp_state v) { __asm__ volatile("vmsr fpscr, %0"::"r"(v)); }
static int pl_fp_equal(pl_fp_state a,pl_fp_state b) { return a==b; }
#else
typedef fenv_t pl_fp_state;
static pl_fp_state pl_fp_get(void) { fenv_t v;memset(&v,0,sizeof v);fegetenv(&v);return v; }
static void pl_fp_set(pl_fp_state v) { fesetenv(&v); }
static int pl_fp_equal(pl_fp_state a,pl_fp_state b) {
    pl_fp_state saved=pl_fp_get();pl_fp_set(a);int ae=fetestexcept(FE_ALL_EXCEPT),ar=fegetround();
    pl_fp_set(b);int be=fetestexcept(FE_ALL_EXCEPT),br=fegetround();pl_fp_set(saved);return ae==be&&ar==br;
}
#endif
static void xv_point_location_hook(xctx *c,void (*guest)(xctx *))
{
    int mode=__atomic_load_n(&pl_mode,__ATOMIC_RELAXED);
    if(mode<0) { const char *e=getenv("XV_POINT_LOCATION");mode=e?atoi(e):0;
        if(mode!=1&&mode!=2)mode=0;int expected=-1;
        __atomic_compare_exchange_n(&pl_mode,&expected,mode,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED);
        mode=__atomic_load_n(&pl_mode,__ATOMIC_RELAXED); }
    if(!mode) { guest(c);return; }
    if((c->r[4]&3u)||c->r[4]<8u||(xv_object_is_worker_thread&&xv_object_is_worker_thread())) {
        __atomic_fetch_add(&pl_declined,1,__ATOMIC_RELAXED);guest(c);return;
    }
    if(mode==2) { __atomic_fetch_add(&pl_native,1,__ATOMIC_RELAXED);xv_point_location_body(c);return; }
    const int budget=1<<28;
    xctx input=*c;
    uint32_t stack=input.r[4]-8u;
    unsigned char before[8],after[8],actual[8];
    x_guest_read(before,stack,8);
    pl_fp_state fp=pl_fp_get();
    c->preempt=budget;xv_point_location_body(c);
    xctx native=*c;pl_fp_state nfp=pl_fp_get();x_guest_read(after,stack,8);
    x_guest_write(stack,before,8);*c=input;c->preempt=budget;pl_fp_set(fp);
    guest(c);pl_fp_state gfp=pl_fp_get();x_guest_read(actual,stack,8);
    int equal=!memcmp(&native,c,sizeof *c)&&!memcmp(after,actual,8)&&pl_fp_equal(nfp,gfp);
    __atomic_fetch_add(&pl_verified,1,__ATOMIC_RELAXED);
    if(!equal) {
        unsigned n=__atomic_fetch_add(&pl_mismatch,1,__ATOMIC_RELAXED);
        __atomic_store_n(&pl_mode,0,__ATOMIC_RELAXED);
        if(n<4)xk_os_log("[point-location] MISMATCH: native disabled; guest result retained\n");
    }
    int64_t remaining=(int64_t)input.preempt-(budget-c->preempt);
    c->preempt=remaining<INT32_MIN?INT32_MIN:(int32_t)remaining;
    if(c->preempt<=0)xv_preempt(c);
}
void xv_point_location_report(unsigned frames)
{
    if(__atomic_load_n(&pl_mode,__ATOMIC_RELAXED)<=0&&!__atomic_load_n(&pl_verified,__ATOMIC_RELAXED))return;
    xk_os_log("[point-location] %u frames; session native %u verified %u mismatches %u declined %u\n",frames,
        __atomic_load_n(&pl_native,__ATOMIC_RELAXED),__atomic_load_n(&pl_verified,__ATOMIC_RELAXED),
        __atomic_load_n(&pl_mismatch,__ATOMIC_RELAXED),__atomic_load_n(&pl_declined,__ATOMIC_RELAXED));
}
