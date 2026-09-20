#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_captured_task.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

static xv_captured_task task;
static unsigned stop,entered,release_task;
struct payload { unsigned input,output,calls,wait; };
static void run(void *argument)
{
    struct payload *p=argument;
    p->calls++;
    if(p->wait) {
        __atomic_store_n(&entered,1,__ATOMIC_RELEASE);
        while(!__atomic_load_n(&release_task,__ATOMIC_ACQUIRE))sched_yield();
    }
    p->output=p->input*17u+3u;
}
static void *helper(void *unused)
{
    (void)unused;
    while(!__atomic_load_n(&stop,__ATOMIC_ACQUIRE))
        if(!xv_captured_try_run(&task))sched_yield();
    return NULL;
}
int main(void)
{
    assert(!xv_captured_try_run(&task)&&!xv_captured_retire(&task));
    struct payload local={11,0,0,0};
    xv_captured_offer(&task,run,&local);
    assert(xv_captured_try_run(&task));
    assert(!xv_captured_try_run(&task));
    assert(xv_captured_retire(&task)&&local.calls==1&&local.output==190);
    pthread_t thread;assert(!pthread_create(&thread,NULL,helper,NULL));
    struct payload claimed={19,0,0,1};
    xv_captured_offer(&task,run,&claimed);
    while(!__atomic_load_n(&entered,__ATOMIC_ACQUIRE))sched_yield();
    assert(!xv_captured_try_run(&task)&&!xv_captured_retire(&task));
    __atomic_store_n(&release_task,1,__ATOMIC_RELEASE);
    while(!xv_captured_retire(&task))sched_yield();
    assert(claimed.calls==1&&claimed.output==326);
    for(unsigned i=0;i<20000;i++) {
        struct payload next={i,0,0,0};
        xv_captured_offer(&task,run,&next);
        /* Race publisher fallback against helper, then reuse stack/slot. */
        xv_captured_try_run(&task);
        while(!xv_captured_retire(&task))sched_yield();
        assert(next.calls==1&&next.output==i*17u+3u);
    }
    __atomic_store_n(&stop,1,__ATOMIC_RELEASE);
    assert(!pthread_join(thread,NULL));
    puts("PASS captured task: fallback, claimed join, exact-once, 20000 slot reuses");
}
