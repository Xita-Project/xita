#include "kernel/xk_occlusion_results.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
static xv_occl_results q;
enum {N=1000000};
static xv_occl_event event(uint32_t n){return (xv_occl_event){n,n^0xabcdef01u,n*3,n*7,~n};}
static void *producer(void *unused){
    (void)unused;
    for(uint32_t n=0;n<N;n++)while(!xv_occl_results_push(&q,event(n)))sched_yield();
    return NULL;
}
int main(void){
    q.head=q.tail=UINT32_MAX-512u;
    for(unsigned i=0;i<XV_OCCL_RESULT_CAP;i++)assert(xv_occl_results_push(&q,event(i)));
    assert(!xv_occl_results_push(&q,event(9999)));assert(q.dropped==1);
    for(unsigned i=0;i<XV_OCCL_RESULT_CAP;i++){xv_occl_event e,w=event(i);assert(xv_occl_results_pop(&q,&e));assert(!memcmp(&e,&w,sizeof e));}
    xv_occl_event e;assert(!xv_occl_results_pop(&q,&e));
    /* Queue reset only while neither endpoint is running. */
    memset(&q,0,sizeof q);q.head=q.tail=UINT32_MAX-512u;
    pthread_t p;assert(!pthread_create(&p,NULL,producer,NULL));
    for(unsigned i=0;i<N;i++){
        while(!xv_occl_results_pop(&q,&e))sched_yield();
        xv_occl_event w=event(i);assert(!memcmp(&e,&w,sizeof e));
    }
    assert(!pthread_join(p,NULL));assert(!xv_occl_results_pop(&q,&e));
    puts("PASS wraparound, full/empty, sticky overflow, 1000000 ordered concurrent payloads");
}
