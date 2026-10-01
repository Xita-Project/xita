/* Exercise the real host ucontext implementation and a foreign native thread. */
#include "kernel/xk_os.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#ifdef TEST_SERIAL_ADMISSION
#include "kernel/xk_object_jobs.c"
static xk_thread guest;
xk_thread *xk_cur=&guest;
static void check_serial(void)
{
    guest.fiber=xk_os_fiber_current();guest.state=0;guest.ctx.fiber=NULL;
    assert(xv_object_feature_serial_admit(&guest.ctx));
    assert(!xv_object_feature_serial_admit(NULL));
    guest.state=1;assert(!xv_object_feature_serial_admit(&guest.ctx));guest.state=0;
    guest.ctx.fiber=(void *)&xv_object_job_marker;
    assert(!xv_object_feature_serial_admit(&guest.ctx));guest.ctx.fiber=NULL;
    initialized=-1;assert(!xv_object_feature_serial_admit(&guest.ctx));
    initialized=1;assert(!xv_object_feature_serial_admit(&guest.ctx));initialized=0;
    owner=&guest.ctx;assert(!xv_object_feature_serial_admit(&guest.ctx));owner=NULL;
    unsigned *busy[]={&admitting,&count,&running,&pause_workers,&owner_notice};
    for(unsigned i=0;i<sizeof busy/sizeof *busy;i++){
        *busy[i]=1;assert(!xv_object_feature_serial_admit(&guest.ctx));*busy[i]=0;
    }
    audio_service_context=&guest.ctx;assert(!xv_object_feature_serial_admit(&guest.ctx));audio_service_context=NULL;
    assert(xv_object_feature_serial_admit(&guest.ctx));
}
#endif
static xk_fiber *first,*second;
static unsigned first_runs,second_runs;
static void *foreign(void *unused)
{
    (void)unused;
    assert(!xk_os_fiber_is_current_guest());
#ifdef TEST_SERIAL_ADMISSION
    /* Actual foreign pthread: reject even a deliberately invalid context. */
    assert(!xv_object_feature_serial_admit((const xctx *)(uintptr_t)1));
#endif
    return NULL;
}
static void check_guest(void)
{
    pthread_t thread;
#ifdef TEST_SERIAL_ADMISSION
    check_serial();
#endif
    assert(xk_os_fiber_is_current_guest());
    assert(!pthread_create(&thread,NULL,foreign,NULL));
    assert(!pthread_join(thread,NULL));
    assert(xk_os_fiber_is_current_guest());
}
static void run_first(void *unused)
{
    (void)unused;
    for(;;){
        assert(xk_os_fiber_current()==first);check_guest();first_runs++;
        xk_os_fiber_switch(second);
        assert(xk_os_fiber_current()==first);check_guest();
        xk_os_fiber_switch(xk_os_fiber_main());
    }
}
static void run_second(void *unused)
{
    (void)unused;
    for(;;){assert(xk_os_fiber_current()==second);check_guest();second_runs++;xk_os_fiber_switch(first);}
}
int main(void)
{
    assert(!xk_os_fiber_is_current_guest());
    first=xk_os_fiber_create(run_first,NULL,65536);
    second=xk_os_fiber_create(run_second,NULL,65536);
    for(unsigned i=0;i<10;i++){
        assert(!xk_os_fiber_is_current_guest());xk_os_fiber_switch(first);
        assert(!xk_os_fiber_is_current_guest());assert(first_runs==i+1&&second_runs==i+1);
    }
    xk_os_fiber_destroy(first);xk_os_fiber_destroy(second);
    assert(!xk_os_fiber_is_current_guest());
    puts("PASS actual fibers: scheduler rejected, guest identity survives switches, foreign pthread rejected");
}
