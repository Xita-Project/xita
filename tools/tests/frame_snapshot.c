#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../recomp/kernel/xk_frame_snapshot.h"
static xv_frame_snapshot q;
static uint64_t buffers[2][512];
static unsigned finished;
static void *producer(void *unused)
{
    (void)unused;
    for(uint64_t n=1;n<=50000;n++) {
        uint64_t *p;
        while(!(p=xv_frame_snapshot_begin(&q)))sched_yield();
        for(unsigned i=0;i<512;i++)p[i]=n^(uint64_t)i;
        assert(xv_frame_snapshot_publish(&q,sizeof buffers[0]));
    }
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);return NULL;
}
int main(void)
{
    xv_snapshot_view v;
    assert(!xv_frame_snapshot_init(&q,buffers[0],buffers[0],sizeof buffers[0]));
    assert(!xv_frame_snapshot_init(&q,buffers[0],(char *)buffers[0]+8,sizeof buffers[0]));
    assert(xv_frame_snapshot_init(&q,buffers[0],buffers[1],sizeof buffers[0]));
    assert(!xv_frame_snapshot_acquire(&q,&v));
    uint64_t *p=xv_frame_snapshot_begin(&q);assert(p);p[0]=101;
    assert(!xv_frame_snapshot_begin(&q));
    assert(!xv_frame_snapshot_publish(&q,sizeof buffers[0]+1));
    assert(xv_frame_snapshot_publish(&q,8));
    assert(xv_frame_snapshot_acquire(&q,&v) && v.size==8 && v.generation==1);
    assert(!xv_frame_snapshot_acquire(&q,&v));
    p=xv_frame_snapshot_begin(&q);assert(p);p[0]=202;
    assert(xv_frame_snapshot_publish(&q,8));
    assert(!xv_frame_snapshot_begin(&q)); /* old generation still pinned */
    assert(*(const uint64_t *)v.data==101);
    xv_frame_snapshot_release(&q);
    assert(xv_frame_snapshot_acquire(&q,&v) && v.generation==2);
    assert(*(const uint64_t *)v.data==202);xv_frame_snapshot_release(&q);
    assert(xv_frame_snapshot_begin(&q));xv_frame_snapshot_cancel(&q);
    assert(xv_frame_snapshot_acquire(&q,&v) && v.generation==2);xv_frame_snapshot_release(&q);
    q.next_generation=UINT64_MAX;assert(!xv_frame_snapshot_begin(&q));
    assert(xv_frame_snapshot_init(&q,buffers[0],buffers[1],sizeof buffers[0]));
    pthread_t t;assert(!pthread_create(&t,NULL,producer,NULL));uint64_t last=0;unsigned reads=0;
    while(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE) || last<50000) {
        if(!xv_frame_snapshot_acquire(&q,&v)){sched_yield();continue;}
        const uint64_t *a=v.data;uint64_t n=a[0];
        assert(v.size==sizeof buffers[0] && v.generation==n && n>=last);
        for(unsigned repeat=0;repeat<3;repeat++) {
            for(unsigned i=0;i<512;i++)assert(a[i]==(n^(uint64_t)i));
            sched_yield();
        }
        last=n;reads++;xv_frame_snapshot_release(&q);
    }
    assert(!pthread_join(t,NULL));printf("PASS 50000 publications, %u pinned reads, no torn payloads\n",reads);
}
