#define _POSIX_C_SOURCE 200809L
/* Actual combined clip/polygon startup helpers, production pool and controllers.
 * The pool starts its real pthread workers. Queue/service states are arranged
 * while they sleep, except for one real execute() callback. Platform doubles:
 * guest arena allocation, clock/logging, current-fiber identity, benchmark and
 * census mode/request storage. This is not a Vita scheduler/fiber proof. */
#include "../../recomp/kernel/xk_object_jobs.c"
#include "../../recomp/kernel/xk_clip_trial.h"
#include "../../recomp/kernel/xk_polygon_edge_trial.h"
#include <assert.h>
#include <stdarg.h>
#include <fenv.h>
#include <string.h>

enum { ARENA_BYTES=2u<<20, PAGE_BYTES=4096 };
uint8_t *g_xram,*g_img_base;
uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;
int xv_watch_n,xv_trace_funcs,xv_phase_enabled;
unsigned xv_light_census_enabled,xv_light_census_present_requested;
xk_thread *xk_cur;
static xk_thread t_guest;
static int t_fiber_cookie,t_other_fiber;
static xk_fiber *t_current;
static unsigned t_alloc,t_benchmark,t_init,t_override,t_compat,t_logs,t_checks,t_worker;
static char t_last_log[256],t_edge_log[256];
static unsigned t_edge_init,t_edge_override,t_edge_logs;

extern void __real_xv_native_clip_region_init(void);
extern void __real_xv_native_clip_region_override(int);
extern int __real_xv_clip_region_compatible(void);
extern void xv_clip_registers_override(int);
extern void __real_xv_native_polygon_edge_init(void);
extern void __real_xv_native_polygon_edge_override(int);
void __wrap_xv_native_polygon_edge_init(void) {t_edge_init++;__real_xv_native_polygon_edge_init();}
void __wrap_xv_native_polygon_edge_override(int n) {t_edge_override++;__real_xv_native_polygon_edge_override(n);}
void __wrap_xv_native_clip_region_init(void) { t_init++; __real_xv_native_clip_region_init(); }
void __wrap_xv_native_clip_region_override(int n) { t_override++; __real_xv_native_clip_region_override(n); }
int __wrap_xv_clip_region_compatible(void) { t_compat++; return __real_xv_clip_region_compatible(); }
int xv_benchmark_active(void) { return t_benchmark; }
xk_fiber *xk_os_fiber_current(void) { return t_current; }
uint64_t xk_os_monotonic_us(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (uint64_t)ts.tv_sec*1000000+ts.tv_nsec/1000; }
void xk_os_log(const char *fmt,...) {
    if (!strncmp(fmt,"[polygon-edge-trial]",20)) {
        va_list ap;va_start(ap,fmt);vsnprintf(t_edge_log,sizeof t_edge_log,fmt,ap);va_end(ap);t_edge_logs++;return;
    }
    if (strncmp(fmt,"[clip-region-trial]",19)) return;
    va_list ap;va_start(ap,fmt);vsnprintf(t_last_log,sizeof t_last_log,fmt,ap);va_end(ap);t_logs++;
}
uint32_t xk_mem_alloc(uint32_t n,uint32_t a,uint32_t lo,uint32_t hi,int top) {
    (void)a;(void)lo;(void)hi;(void)top;assert(n==STACK_BYTES);assert(t_alloc<LANES);
    return 0x100000+(t_alloc++)*STACK_BYTES;
}
int xk_mem_free(uint32_t p) { (void)p;return 1; }
int xk_object_io_step(void) { abort(); }
void xv_light_census_cancel(xctx *c,unsigned why) { (void)c;(void)why; }

/* Snapshot the entire context and mapped guest arena/table around the actual
 * helper. The test never patches or resets its private completed bit. */
static void t_call(xctx *c,int readable) {
    xctx before={0}, owner_before=t_guest.ctx;
    uint8_t *memory=malloc(ARENA_BYTES),*saved_ram=g_xram,*saved_img=g_img_base;
    uint32_t *pages=malloc(ARENA_BYTES/PAGE_BYTES*sizeof *pages),*saved_pt=g_xpt;
    assert(memory&&pages);memcpy(memory,g_xram,ARENA_BYTES);
    memcpy(pages,g_xpt,ARENA_BYTES/PAGE_BYTES*sizeof *pages);
    if(readable)memcpy(&before,c,sizeof before);
    fenv_t fp;assert(!fegetenv(&fp));int exceptions=fetestexcept(FE_ALL_EXCEPT),round=fegetround();
    XV_CLIP_TRIAL_PRESENT(c);
    XV_POLYGON_EDGE_TRIAL_PRESENT(c);
    assert(g_xram==saved_ram&&g_img_base==saved_img&&g_xpt==saved_pt);
    assert(!memcmp(memory,g_xram,ARENA_BYTES));assert(!memcmp(pages,g_xpt,ARENA_BYTES/PAGE_BYTES*sizeof *pages));
    assert(!memcmp(&owner_before,&t_guest.ctx,sizeof owner_before));
    if(readable)assert(!memcmp(&before,c,sizeof before));
    assert(round==fegetround()&&exceptions==fetestexcept(FE_ALL_EXCEPT));
    assert(!fesetenv(&fp));free(memory);free(pages);t_checks++;
}
static void t_unselected(void) { assert(!xv_native_polygon_edge_available()&&!t_edge_init&&!t_edge_override&&!t_edge_logs);assert(!xv_native_clip_region_available()&&!xv_native_clip_region_enabled());assert(!t_init&&!t_override&&!t_compat&&!t_logs); }
static void t_decline(xctx *c,unsigned reason,int readable) {
    assert(xv_object_census_boundary(c)==reason);t_call(c,readable);t_unselected();
}
static void *t_foreign(void *arg) {
    (void)arg;
    /* Deliberately invalid ctx: native ownership must reject before access. */
    t_decline((xctx *)(uintptr_t)1,XV_LC_NATIVE_OWNER,0);
    t_decline(&t_guest.ctx,XV_LC_NATIVE_OWNER,1);return NULL;
}
void f_0008FB70(xctx *c) {
    assert(xv_object_is_worker_thread());
    t_decline(c,XV_LC_WORKER,1);
    t_decline(&t_guest.ctx,XV_LC_WORKER,1);
    t_worker++;c->r[4]+=4;
}
static void t_actual_worker(void) {
    /* The same bounded wake ordering used by clip_region_workers.c, with one
     * existing worker. execute(), stack setup, marker and done semaphore run. */
    next=0;count=1;owner=&t_guest.ctx;memset(&jobs[0],0,sizeof jobs[0]);
    __atomic_store_n(&running,1,__ATOMIC_RELEASE);sem_post(&wakes[0]);wait_sem(&dones[0]);
    __atomic_store_n(&running,0,__ATOMIC_RELEASE);count=0;owner=NULL;owner_notice=0;
    assert(t_worker==1);assert(xv_object_census_boundary(&t_guest.ctx)==XV_LC_OK);
}
static void t_admission_cases(void) {
    pthread_t foreign;assert(!pthread_create(&foreign,NULL,t_foreign,NULL));assert(!pthread_join(foreign,NULL));
    xctx copy=t_guest.ctx;t_decline(&copy,XV_LC_CONTEXT,1);
    xk_cur=NULL;t_decline(&t_guest.ctx,XV_LC_CONTEXT,1);xk_cur=&t_guest;
    t_current=(xk_fiber *)&t_other_fiber;t_decline(&t_guest.ctx,XV_LC_CONTEXT,1);t_current=t_guest.fiber;
    t_guest.fiber=NULL;t_decline(&t_guest.ctx,XV_LC_CONTEXT,1);t_guest.fiber=t_current;
    for(int state_i=1;state_i<=3;state_i++){t_guest.state=state_i;t_decline(&t_guest.ctx,XV_LC_CONTEXT,1);}t_guest.state=0;
    void *saved=t_guest.ctx.fiber;t_guest.ctx.fiber=(void *)&xv_object_job_marker;t_decline(&t_guest.ctx,XV_LC_MARKED,1);t_guest.ctx.fiber=saved;
    contexts[0].fiber=(void *)&xv_object_job_marker;t_decline(&contexts[0],XV_LC_MARKED,1);
    count=1;t_decline(&t_guest.ctx,XV_LC_QUEUE,1);count=0;
    running=1;t_decline(&t_guest.ctx,XV_LC_QUEUE,1);running=0;
    pause_workers=1;t_decline(&t_guest.ctx,XV_LC_QUEUE,1);pause_workers=0;
    owner_notice=1;t_decline(&t_guest.ctx,XV_LC_QUEUE,1);owner_notice=0;
    audio_service_context=&contexts[0];t_decline(&t_guest.ctx,XV_LC_QUEUE,1);audio_service_context=NULL;
    owner=&t_guest.ctx;t_decline(&t_guest.ctx,XV_LC_QUEUE,1);owner=NULL;
    int scope=xv_object_census_scope_begin();assert(scope);t_decline(&t_guest.ctx,XV_LC_GUARD,1);xv_object_census_scope_end(&scope);
    t_actual_worker();
}
static void t_diagnostics(void) {
    unsigned *flags[]={&xv_light_census_enabled,&xv_light_census_present_requested,&t_benchmark};
    for(unsigned i=0;i<sizeof flags/sizeof *flags;i++){*flags[i]=1;t_call(&t_guest.ctx,1);t_unselected();*flags[i]=0;}
    xv_watch_n=1;t_call(&t_guest.ctx,1);t_unselected();xv_watch_n=0;
    xv_trace_funcs=1;t_call(&t_guest.ctx,1);t_unselected();xv_trace_funcs=0;
}
static void *t_completed_foreign(void *arg) {
    (void)arg;t_call((xctx *)(uintptr_t)1,0);return NULL;
}
static void t_enabled_once(void) {
    t_call(&t_guest.ctx,1);assert(xv_native_clip_region_available()&&xv_native_clip_region_enabled());
    assert(xv_native_polygon_edge_available());
    assert(t_init==1&&t_override==1&&t_compat>=1&&t_logs==1);assert(strstr(t_last_log,"startup enabled 1"));
    assert(t_edge_init==1&&t_edge_override==1&&t_edge_logs==1);assert(strstr(t_edge_log,"startup enabled 1"));
    unsigned compat=t_compat;
    for(unsigned i=0;i<5;i++)t_call((xctx *)(uintptr_t)1,0);
    pthread_t foreign;assert(!pthread_create(&foreign,NULL,t_completed_foreign,NULL));assert(!pthread_join(foreign,NULL));
    assert(t_init==1&&t_override==1&&t_compat==compat&&t_logs==1);
    assert(t_edge_init==1&&t_edge_override==1&&t_edge_logs==1);
    assert(xv_polygon_edge_begin());xv_polygon_edge_end();assert(xv_math_polygon_edge_calls()==1);
    xv_clip_region_work work={.regions=1,.planes=3,.clips=2,.input_vertices=7,.max_clips=2},got;
    assert(xv_clip_region_begin());xv_clip_region_end(&work);xv_clip_region_read_work(&got);
    assert(got.regions==1&&got.clips==2&&got.planes==3&&got.input_vertices==7&&got.max_clips==2);
}
static void t_preinitialized(const char *mode) {
    int both=!strcmp(mode,"both-preinit-active");
    int edge=both||!strncmp(mode,"edge-",5),clip=both||!strncmp(mode,"clip-",5);
    int active=strstr(mode,"active")!=NULL,enabled=strstr(mode,"off")==NULL;
    if(edge){xv_native_polygon_edge_init();xv_native_polygon_edge_override(enabled);if(active)assert(xv_polygon_edge_begin());}
    if(clip){xv_native_clip_region_init();xv_native_clip_region_override(enabled);if(active)assert(xv_clip_region_begin());}
    t_init=t_override=t_compat=t_edge_init=t_edge_override=0;
    t_benchmark=1;xv_light_census_present_requested=1;
    t_call(&t_guest.ctx,1);
    assert(xv_native_polygon_edge_available()==edge&&xv_native_clip_region_available()==clip);
    assert(!t_init&&!t_override&&!t_compat&&!t_edge_init&&!t_edge_override);
    assert(t_logs==(unsigned)clip&&t_edge_logs==(unsigned)edge);
    if(edge)assert(strstr(t_edge_log,"existing controller preserved"));
    if(clip)assert(strstr(t_last_log,"existing controller preserved"));
    t_benchmark=0;xv_light_census_present_requested=0;t_call(&t_guest.ctx,1);
    assert(t_init==(unsigned)!clip&&t_override==(unsigned)!clip&&t_edge_init==(unsigned)!edge&&t_edge_override==(unsigned)!edge);
    assert(xv_native_clip_region_enabled()==(clip?enabled:1));
    assert(t_logs==1&&t_edge_logs==1);
    if(edge&&active)xv_polygon_edge_end();
    if(clip&&active){xv_clip_region_work w={.regions=1,.clips=7};xv_clip_region_end(&w);}
    int begin=xv_polygon_edge_begin();assert(begin==(edge?enabled:1));if(begin)xv_polygon_edge_end();
    assert(xv_math_polygon_edge_calls()==(unsigned)((edge&&active)+begin));
    xv_clip_region_work got;xv_clip_region_read_work(&got);assert(got.regions==(unsigned)(clip&&active));assert(got.clips==(clip&&active?7u:0u));
    t_call((xctx *)(uintptr_t)1,0);assert(t_logs==1&&t_edge_logs==1);
}
int main(int argc,char **argv) {
    assert(argc==2);const char *mode=argv[1];
    g_xram=malloc(ARENA_BYTES);g_img_base=g_xram;g_xpt=malloc(ARENA_BYTES/PAGE_BYTES*sizeof *g_xpt);assert(g_xram&&g_xpt);
    memset(g_xram,0xa5,ARENA_BYTES);for(unsigned i=0;i<ARENA_BYTES/PAGE_BYTES;i++)g_xpt[i]=i*PAGE_BYTES;
    memset(&t_guest,0,sizeof t_guest);memset(&t_guest.ctx,0x5a,sizeof t_guest.ctx);
    t_guest.ctx.fiber=NULL;t_guest.fiber=(xk_fiber *)&t_fiber_cookie;t_current=t_guest.fiber;xk_cur=&t_guest;
    assert(!setenv("XV_OBJECT_JOB_WORKERS","2",1));assert(!setenv("XV_OBJECT_LOCK_PROFILE","0",1));
    assert(!setenv("XV_NATIVE_CLIP",!strcmp(mode,"clip-native-off")?"0":"1",1));
    assert(!setenv("XV_CLIP_REGISTERS",!strcmp(mode,"clip-registers-off")?"0":"1",1));
    t_decline((xctx *)(uintptr_t)1,XV_LC_UNINITIALIZED,0);
    assert(initialize());assert(xv_object_census_boundary(&t_guest.ctx)==XV_LC_OK);
    if(!strcmp(mode,"admission")){t_admission_cases();t_diagnostics();t_enabled_once();}
    else if(!strcmp(mode,"clip-native-off")||!strcmp(mode,"clip-registers-off")){
        t_call(&t_guest.ctx,1);assert(!xv_native_clip_region_available()&&!xv_native_clip_region_enabled());
        assert(!t_init&&!t_override&&t_compat==1&&t_logs==1);assert(strstr(t_last_log,"configuration declined"));
        assert(xv_native_polygon_edge_available()&&t_edge_init==1&&t_edge_override==1&&t_edge_logs==1);
        assert(xv_polygon_edge_begin());xv_polygon_edge_end();assert(xv_math_polygon_edge_calls()==1);
        xv_clip_registers_override(1);t_call(&t_guest.ctx,1);assert(t_compat==1&&t_logs==1&&!t_init&&!t_override);
        assert(t_edge_init==1&&t_edge_override==1&&t_edge_logs==1);
    } else if(strstr(mode,"preinit"))t_preinitialized(mode);
    else abort();
    xv_object_jobs_shutdown();free(g_xpt);free(g_xram);
    printf("PASS %s: %u complete-context/arena/page-table/FP checks; real pool and combined clip/polygon controllers\n",mode,t_checks);return 0;
}
