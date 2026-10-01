/* Experimental 868F0 vertex phase. Off unless explicitly enabled at launch.
 * Called only after the generated entry's existing motion-sample scope. */
#include <stdlib.h>
#include <stdio.h>
#if defined(__x86_64__)
#include <xmmintrin.h>
#endif
#include "xk.h"
#include "xk_object_jobs.h"
#include "xk_light_census.h"
#include "xk_feature_scope.h"
#ifndef XV_EXPERIMENTAL_OBJECT_JOBS
#error Native feature scope requires the object ownership backend
#endif
#ifndef XV_LIGHT_QUERY_CENSUS
#error Native feature scope requires native owner identity checks
#endif
#if !XV_QUERY_WORLD_RUN
#error Native feature scope requires retained worker transaction admission
#endif
int xv_scene_thread_on_helper(void) __attribute__((weak));
static int feature_mode=-1;
static unsigned feature_ownership[3+XV_LC_REASONS]; /* null, helper, page table, census reason */
static unsigned feature_selection[5]; /* FP, mapping, short list, height, scope */
static unsigned feature_sizes[9]; /* 0,1,2..7,8..15,16..31,32..63,64..127,128..256,larger */
static unsigned feature_counts[4]; /* calls, ownership refusal, selection/layout refusal, native */
static int feature_enabled(void){
    int value=__atomic_load_n(&feature_mode,__ATOMIC_RELAXED);
    if(value<0){const char *v=getenv("XV_NATIVE_FEATURE_VERTICES");value=v&&v[0]=='1'&&!v[1];__atomic_store_n(&feature_mode,value,__ATOMIC_RELAXED);}
    return value;
}
#ifdef XV_NATIVE_FEATURE_TEST
void xv_native_feature_test_mode(int value){__atomic_store_n(&feature_mode,!!value,__ATOMIC_RELAXED);}
#endif
static int feature_host_fp_supported(void){
#if defined(__arm__)
    uint32_t value;__asm__ volatile("vmrs %0, fpscr" : "=r"(value) :: "memory");
    return !(value&0x00009f00u);
#elif defined(__x86_64__)
    return (_mm_getcsr()&0x1f80u)==0x1f80u;
#else
    return 0;
#endif
}
int xv_native_feature_vertices(xctx *c,xk_feature_guest_call emit,
        xk_feature_guest_call edge,xk_feature_guest_call surface){
    if(!feature_enabled())return 0;
    __atomic_fetch_add(&feature_counts[0],1,__ATOMIC_RELAXED);
    unsigned ownership_reason=0;
    if(!c)goto ownership;
    ownership_reason=1;
    if(xv_scene_thread_on_helper&&xv_scene_thread_on_helper())goto ownership;
    ownership_reason=2;
    if(X_PT!=g_xpt)goto ownership;
    if(!xv_object_world_run_admit(c)){
        unsigned reason=xv_object_census_admit(c);
        if(reason!=XV_LC_OK&&reason!=XV_LC_GUARD&&
           !(reason==XV_LC_UNINITIALIZED&&xv_object_feature_serial_admit(c))){
            ownership_reason=3+(reason<XV_LC_REASONS?reason:XV_LC_CONTEXT);
            goto ownership;
        }
    }
    /* Cheap selection before walking geometry. Pi only supports trying larger
     * extruded lists; record refusal totals to assess actual hardware coverage. */
    unsigned selection_reason=0;
    if(!feature_host_fp_supported())goto layout;
    selection_reason=1;
    uint64_t arena=xk_mem_arena_size();xk_feature_span count,args;
    if(c->r[7]>UINT32_MAX-0x808||
       !xk_feature_admit_span(arena,1u<<20,c->r[7]+0x808,4,&count)||
       !xk_feature_admit_span(arena,1u<<20,c->r[4],24,&args))goto layout;
    float height;memcpy(&height,args.host+12,4);
    uint32_t n=xk_feature_read32(count.host);
    unsigned bin=n==0?0:n==1?1:n<8?2:n<16?3:n<32?4:n<64?5:n<128?6:n<=256?7:8;
    __atomic_fetch_add(&feature_sizes[bin],1,__ATOMIC_RELAXED);
    selection_reason=2;if(n<16)goto layout;
    selection_reason=3;if(!(height>0))goto layout;
    selection_reason=4;
    if(!xk_feature_try_query_vertices(c,arena,1u<<20,emit,edge,surface))goto layout;
    __atomic_fetch_add(&feature_counts[3],1,__ATOMIC_RELAXED);return 1;
ownership:
    __atomic_fetch_add(&feature_ownership[ownership_reason],1,__ATOMIC_RELAXED);
    __atomic_fetch_add(&feature_counts[1],1,__ATOMIC_RELAXED);return 0;
layout:
    __atomic_fetch_add(&feature_selection[selection_reason],1,__ATOMIC_RELAXED);
    __atomic_fetch_add(&feature_counts[2],1,__ATOMIC_RELAXED);return 0;
}
void xv_native_feature_report(unsigned frames){
    (void)frames;if(!feature_enabled())return;
    unsigned v[4];for(unsigned i=0;i<4;i++)v[i]=__atomic_exchange_n(&feature_counts[i],0,__ATOMIC_RELAXED);
    for(unsigned i=0;i<3+XV_LC_REASONS;i++){
        unsigned n=__atomic_exchange_n(&feature_ownership[i],0,__ATOMIC_RELAXED);
        if(n)XK_LOG("[feature-ownership] reason=%u count=%u (0 null, 1 helper, 2 page-table, 3+census enum)\n",i,n);
    }
    unsigned s[5],b[9];
    for(unsigned i=0;i<5;i++)s[i]=__atomic_exchange_n(&feature_selection[i],0,__ATOMIC_RELAXED);
    for(unsigned i=0;i<9;i++)b[i]=__atomic_exchange_n(&feature_sizes[i],0,__ATOMIC_RELAXED);
    if(v[0]){
        XK_LOG("[feature-selection] fp=%u mapping=%u short=%u height=%u scope=%u\n",s[0],s[1],s[2],s[3],s[4]);
        XK_LOG("[feature-sizes] 0/1/2-7/8-15/16-31/32-63/64-127/128-256/over=%u/%u/%u/%u/%u/%u/%u/%u/%u\n",b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8]);
    }
    XK_LOG("[feature-native] calls=%u ownership=%u selection-layout=%u native=%u\n",v[0],v[1],v[2],v[3]);
}
