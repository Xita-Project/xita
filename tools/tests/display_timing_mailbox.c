#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include "../../runtime/xv_display_timing_mailbox.h"
static xv_display_timing_mailbox m;
static void *producer(void *unused) {
    (void)unused;
    for (unsigned i=1;i<=1000000;i++) {
        xv_display_timing_sample s={(uint64_t)i<<33,~((uint64_t)i<<33),(uint64_t)i*99991,i};
        while(!xv_display_timing_push(&m,s))sched_yield();
    }
    return 0;
}
int main(void) {
    xv_display_timing_sample a={1,2,3,4},b={5,6,7,8},r;
    assert(!xv_display_timing_pop(&m,&r));
    assert(xv_display_timing_push(&m,a));assert(!xv_display_timing_push(&m,b));
    assert(xv_display_timing_pop(&m,&r));assert(r.setup_us==1&&r.vblank_us==2&&r.max_us==3&&r.count==4);
    pthread_t t;assert(!pthread_create(&t,0,producer,0));
    for(unsigned i=1;i<=1000000;i++) {
        while(!xv_display_timing_pop(&m,&r))sched_yield();
        assert(r.count==i&&r.setup_us==((uint64_t)i<<33)&&r.vblank_us==~((uint64_t)i<<33)&&r.max_us==(uint64_t)i*99991);
    }
    assert(!pthread_join(t,0));assert(!xv_display_timing_pop(&m,&r));
    puts("PASS: empty/full ownership and 1000000 ordered 64-bit payloads");
}
