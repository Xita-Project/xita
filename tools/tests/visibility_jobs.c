/* Existing object worker backend, copied native visibility packets, real wakes
 * and joins. Guest allocation/fiber/clock fixtures come from the prior tests. */
#define main fixture_main
#include "visibility_backend_fixture.c"
#undef main
#include "../../recomp/kernel/xk_visibility_jobs.h"
#include <xmmintrin.h>
extern int __real_fegetenv(fenv_t *);
extern xs_bounds_result __real_xs_bounds(const xs_frustum *,const xs_box *);
extern int feenableexcept(int),fedisableexcept(int);
static unsigned checks,expected_mxcsr,base_mxcsr;
static int expected_round,base_round,base_exceptions;
static unsigned seen[3],worker_saves[2];
static int blocking;
static sem_t entered,release_worker;
static xv_visibility_input *caller_input;
static xs_bounds_result *caller_output;
static unsigned caller_n;
int __wrap_fegetenv(fenv_t *f)
{
    int lane=worker_lane();
    if(lane>=0 && __atomic_load_n(&visibility_running,__ATOMIC_ACQUIRE)) {
        assert(fegetround()==base_round && fetestexcept(FE_ALL_EXCEPT)==base_exceptions);
        assert(_mm_getcsr()==base_mxcsr);worker_saves[lane]++;
    }
    return __real_fegetenv(f);
}
xs_bounds_result __wrap_xs_bounds(const xs_frustum *f,const xs_box *b)
{
    if(!__atomic_load_n(&visibility_running,__ATOMIC_ACQUIRE))return __real_xs_bounds(f,b);
    int lane=worker_lane();unsigned index=lane<0?2:(unsigned)lane;seen[index]++;
    assert(fegetround()==expected_round);
    assert((_mm_getcsr()&0xffc0u)==(expected_mxcsr&0xffc0u));
    assert(!xv_visibility_classify_jobs((xctx *)(uintptr_t)1,
        (const xv_visibility_input *)(uintptr_t)1,1,(xs_bounds_result *)(uintptr_t)1));
    if(lane==0 && __atomic_exchange_n(&blocking,0,__ATOMIC_ACQ_REL)) {
        assert(!sem_post(&entered));wait_sem(&release_worker);
    }
    return __real_xs_bounds(f,b);
}
static void fill(xv_visibility_input *p,unsigned n)
{
    for(unsigned i=0;i<n;++i) {
        p[i].frustum=(xs_frustum){.plane={{1,.25,-.5,2},{-1,.5,.25,2},{.25,1,.5,2},{.5,-1,-.25,2}},
            .enclosing={.axis={{-8,8},{-8,8},{-8,8}}}};
        for(unsigned a=0;a<3;++a) {
            p[i].box.axis[a][0]=((int)((i*(a+3))%67)-33)*.25f;
            p[i].box.axis[a][1]=p[i].box.axis[a][0]+(i%9)*.125f;
        }
        /* Tiny input coefficients exercise denormal-input FP controls too. */
        if(i%7==0) { unsigned tiny=1;memcpy(&p[i].frustum.plane[0][0],&tiny,4); }
    }
}
static void *interfere(void *unused)
{
    (void)unused;wait_sem(&entered);
    /* Workers must own copies; mutation after capture cannot affect results. */
    memset(caller_input,0xa5,caller_n*sizeof(*caller_input));
    for(unsigned i=0;i<caller_n*sizeof(*caller_output);++i)assert(((unsigned char*)caller_output)[i]==0xa5);
    assert(!xv_visibility_classify_jobs(&t_guest.ctx,(const xv_visibility_input *)(uintptr_t)1,
        512,(xs_bounds_result *)(uintptr_t)1));
    xv_object_jobs_report(1); /* Live native work must prevent counter reset. */
    assert(!sem_post(&release_worker));return NULL;
}
static void run(unsigned n,int mutate)
{
    xv_visibility_input input[XV_VISIBILITY_JOB_CAPACITY],copy[XV_VISIBILITY_JOB_CAPACITY];
    xs_bounds_result reference[XV_VISIBILITY_JOB_CAPACITY],output[XV_VISIBILITY_JOB_CAPACITY];
    fill(input,n);memcpy(copy,input,n*sizeof(*input));
    for(unsigned i=0;i<n;++i)reference[i]=__real_xs_bounds(&input[i].frustum,&input[i].box);
    /* Preserve the owner's sticky flags as well as its controls. */
    assert(!feraiseexcept(FE_DIVBYZERO));int flags=fetestexcept(FE_ALL_EXCEPT),round=fegetround();
    unsigned mxcsr=_mm_getcsr();expected_round=round;expected_mxcsr=mxcsr;
    memset(output,0xa5,sizeof(output));xctx saved=t_guest.ctx;
    unsigned char *memory=malloc(ARENA_BYTES);assert(memory);memcpy(memory,g_xram,ARENA_BYTES);
    unsigned *pages=malloc(ARENA_BYTES/PAGE_BYTES*sizeof(unsigned));assert(pages);memcpy(pages,g_xpt,ARENA_BYTES/PAGE_BYTES*sizeof(unsigned));
    pthread_t observer;
    if(mutate) {
        caller_input=input;caller_output=output;caller_n=n;blocking=1;
        assert(!pthread_create(&observer,NULL,interfere,NULL));
    }
    assert(xv_visibility_classify_jobs(&t_guest.ctx,input,n,output));
    if(mutate)assert(!pthread_join(observer,NULL));
    else assert(!memcmp(input,copy,n*sizeof(*input)));
    assert(!memcmp(output,reference,n*sizeof(*output)));
    for(unsigned i=n*sizeof(*output);i<sizeof(output);++i)assert(((unsigned char*)output)[i]==0xa5);
    assert(!memcmp(&saved,&t_guest.ctx,sizeof(saved)) && !memcmp(memory,g_xram,ARENA_BYTES));
    assert(!memcmp(pages,g_xpt,ARENA_BYTES/PAGE_BYTES*sizeof(unsigned)));free(memory);free(pages);
    assert(fegetround()==round && fetestexcept(FE_ALL_EXCEPT)==flags && _mm_getcsr()==mxcsr);
    assert(!running&&!visibility_running&&!count&&!owner_notice&&!owner);
    assert(sem_trywait(&owner_wake)<0&&errno==EAGAIN);
    for(unsigned i=0;i<WORKERS;++i)assert(sem_trywait(&dones[i])<0&&errno==EAGAIN);
    checks++;
}
static void decline(xctx *c,const xv_visibility_input *input,unsigned n,xs_bounds_result *out)
{
    xctx saved=t_guest.ctx;xs_bounds_result before=*out;
    int flags=fetestexcept(FE_ALL_EXCEPT),round=fegetround();unsigned mxcsr=_mm_getcsr();
    assert(!xv_visibility_classify_jobs(c,input,n,out));
    assert(!memcmp(&saved,&t_guest.ctx,sizeof(saved))&&!memcmp(out,&before,sizeof(before)));
    assert(flags==fetestexcept(FE_ALL_EXCEPT)&&round==fegetround()&&mxcsr==_mm_getcsr());checks++;
}
int main(void)
{
    g_xram=malloc(ARENA_BYTES);g_img_base=g_xram;g_xpt=malloc(ARENA_BYTES/PAGE_BYTES*sizeof(unsigned));assert(g_xram&&g_xpt);
    memset(g_xram,0x5a,ARENA_BYTES);for(unsigned i=0;i<ARENA_BYTES/PAGE_BYTES;++i)g_xpt[i]=i*PAGE_BYTES;
    memset(&t_guest,0,sizeof(t_guest));t_guest.fiber=(xk_fiber*)&t_fiber_cookie;t_current=t_guest.fiber;xk_cur=&t_guest;
    assert(!sem_init(&entered,0,0)&&!sem_init(&release_worker,0,0));
    xv_visibility_input input;fill(&input,1);xs_bounds_result output={0x1234,0x5678};
    decline((xctx*)(uintptr_t)1,(const xv_visibility_input*)(uintptr_t)1,1,&output);
    fesetround(FE_TONEAREST);feclearexcept(FE_ALL_EXCEPT);feraiseexcept(FE_DIVBYZERO);
    base_round=fegetround();base_exceptions=fetestexcept(FE_ALL_EXCEPT);base_mxcsr=_mm_getcsr();
    setenv("XV_OBJECT_JOB_WORKERS","2",1);assert(initialize());
    unsigned sizes[]={1,2,7,8,9,23,24,25,127,128,129,181,511,512};
    int modes[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};
    for(unsigned workers=0;workers<=2;++workers) {
        active_workers=workers;
        for(unsigned mode=0;mode<4;++mode)for(unsigned denorm=0;denorm<4;++denorm) {
            fesetround(modes[mode]);unsigned csr=_mm_getcsr();csr&=~0x8040u;
            if(denorm&1)csr|=0x8000u;if(denorm&2)csr|=0x40u;_mm_setcsr(csr);
            for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);++i)run(sizes[i],0);
        }
    }
    /* Ownership/queue/diagnostic/invalid-packet rejection never starts jobs. */
    for(unsigned mode=0;mode<4;++mode) {
        fesetround(modes[mode]);
        decline((xctx*)(uintptr_t)1,(const xv_visibility_input*)(uintptr_t)1,1,&output);
        count=1;decline(&t_guest.ctx,&input,1,&output);count=0;
        running=1;decline(&t_guest.ctx,&input,1,&output);running=0;
        visibility_running=1;decline(&t_guest.ctx,&input,1,&output);visibility_running=0;
        stopping=1;decline(&t_guest.ctx,&input,1,&output);stopping=0;
        owner=&t_guest.ctx;decline(&t_guest.ctx,&input,1,&output);owner=NULL;
        pause_workers=1;decline(&t_guest.ctx,&input,1,&output);pause_workers=0;
        owner_notice=1;decline(&t_guest.ctx,&input,1,&output);owner_notice=0;
        override=0;decline(&t_guest.ctx,&input,1,&output);override=-1;
        xv_phase_enabled=1;decline(&t_guest.ctx,&input,1,&output);xv_phase_enabled=0;
        xv_watch_n=1;decline(&t_guest.ctx,&input,1,&output);xv_watch_n=0;
        xv_trace_funcs=1;decline(&t_guest.ctx,&input,1,&output);xv_trace_funcs=0;
        xv_light_census_enabled=1;decline(&t_guest.ctx,&input,1,&output);xv_light_census_enabled=0;
        decline(&t_guest.ctx,NULL,1,&output);decline(&t_guest.ctx,&input,0,&output);decline(&t_guest.ctx,&input,513,&output);
        unsigned invalid=0x7f801234;memcpy(&input.box.axis[0][0],&invalid,4);decline(&t_guest.ctx,&input,1,&output);fill(&input,1);
        input.box.axis[0][0]=100;decline(&t_guest.ctx,&input,1,&output);fill(&input,1);
    }
    feclearexcept(FE_ALL_EXCEPT);assert(feenableexcept(FE_DIVBYZERO)>=0);
    decline(&t_guest.ctx,(const xv_visibility_input*)(uintptr_t)1,1,&output);
    assert(fedisableexcept(FE_ALL_EXCEPT)>=0);
    /* Actual legacy object callbacks and pure jobs may alternate on the same
     * wake/done semaphores. A stale native-mode bit must not intercept them. */
    fesetround(FE_TONEAREST);_mm_setcsr(base_mxcsr);
    for(unsigned i=0;i<32;++i) {
        count=1;owner=&t_guest.ctx;memset(&jobs[0],0,sizeof(jobs[0]));xv_object_jobs_join();owner=NULL;
        run(181,0);
    }
    run(512,1);
    assert(seen[0]&&seen[1]&&seen[2]&&worker_saves[0]&&worker_saves[1]);
    printf("PASS %u calls/declines, actual native lanes %u/%u/%u items, worker FP restores %u/%u, legacy callbacks %u\n",
        checks,seen[0],seen[1],seen[2],worker_saves[0],worker_saves[1],t_worker);
    for(unsigned lane=0;lane<3;++lane)assert(visibility_lane[lane].items==seen[lane]);
    xv_object_jobs_report(1);
    for(unsigned lane=0;lane<3;++lane)assert(!visibility_lane[lane].items&&!visibility_lane[lane].calls);
    xv_object_jobs_shutdown();sem_destroy(&entered);sem_destroy(&release_worker);free(g_xpt);free(g_xram);return 0;
}
