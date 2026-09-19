#include "../../recomp/kernel/xk_render_snapshot.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static xv_render_snapshots q;
static xv_render_state storage[2];
static unsigned finished, observed;
static void fill(float p[64][13],unsigned tick)
{for(unsigned i=0;i<64;i++)for(unsigned j=0;j<13;j++)p[i][j]=(float)(tick*1024+i*13+j);}
static void *producer(void *unused)
{
    (void)unused;float p[64][13];
    for(unsigned t=1;t<=10000;t++) {
        while(!xv_render_snapshots_begin(&q,99,t))sched_yield();
        fill(p,t);
        assert(xv_render_snapshots_add(&q,0x12340005,77,64,&p[0][0]));
        memset(p,0,sizeof p); /* copied input must outlive/mutate independently */
        if(t&1)assert(xv_render_snapshots_add(&q,0x56780006,88,64,&p[0][0]));
        assert(xv_render_snapshots_publish(&q));
    }
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);return NULL;
}
static void *consumer(void *unused)
{
    (void)unused;uint64_t last=0;
    do {
        const xv_render_state *s=xv_render_snapshots_acquire(&q,99);
        if(!s) {sched_yield();continue;}
        assert(s->tick>=last);last=s->tick;
        const xv_render_pose *p=xv_render_snapshot_find(s,0x12340005,77);
        assert(p&&p->nodes==64);
        assert(!xv_render_snapshot_find(s,0x99990005,77)); /* recycled index */
        assert(!xv_render_snapshot_find(s,0x12340005,78)); /* replaced model */
        for(unsigned i=0;i<64;i++) {
            sched_yield();
            for(unsigned j=0;j<13;j++)assert(p->matrices[i][j]==(float)(s->tick*1024+i*13+j));
        }
        assert(s->count==1+(s->tick&1));
        assert(!!xv_render_snapshot_find(s,0x56780006,88)==!!(s->tick&1));
        observed++;
        xv_render_snapshots_release(&q);
    } while(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE)||last<10000);
    return NULL;
}
int main(void)
{
    float p[64][13];fill(p,1);
    assert(xv_render_snapshots_init(&q,&storage[0],&storage[1]));
    assert(!xv_render_snapshots_acquire(&q,1));
    assert(xv_render_snapshots_begin(&q,1,1));
    assert(xv_render_snapshots_add(&q,1,2,64,&p[0][0]));
    assert(xv_render_snapshots_publish(&q));
    assert(!xv_render_snapshots_acquire(&q,2));
    const xv_render_state *old=xv_render_snapshots_acquire(&q,1);assert(old);
    assert(xv_render_snapshots_begin(&q,1,2));
    assert(xv_render_snapshots_add(&q,1,2,1,&p[0][0]));
    assert(xv_render_snapshots_publish(&q));
    assert(old->tick==1&&old->poses[0].nodes==64);
    assert(!xv_render_snapshots_begin(&q,1,3)); /* pinned old slot */
    xv_render_snapshots_release(&q);
    assert(xv_render_snapshots_begin(&q,1,3));
    assert(xv_render_snapshots_add(&q,1,2,1,&p[0][0]));
    assert(!xv_render_snapshots_add(&q,1,3,1,&p[0][0]));
    assert(!xv_render_snapshots_publish(&q)); /* reject whole duplicate list */
    old=xv_render_snapshots_acquire(&q,1);assert(old&&old->tick==2);
    xv_render_snapshots_release(&q);
    assert(xv_render_snapshots_begin(&q,2,4));
    assert(xv_render_snapshots_publish(&q)); /* empty scene removes all objects */
    assert(!xv_render_snapshots_acquire(&q,1));
    old=xv_render_snapshots_acquire(&q,2);assert(old&&old->count==0);
    xv_render_snapshots_release(&q);
    for(unsigned bad=0;bad<5;bad++) {
        assert(xv_render_snapshots_begin(&q,2,5));
        assert(!xv_render_snapshots_add(&q,bad==0?UINT32_MAX:1,bad==1?UINT32_MAX:2,
                 bad==2?0:bad==3?65:1,bad==4?NULL:&p[0][0]));
        assert(!xv_render_snapshots_publish(&q));
    }
    assert(xv_render_snapshots_begin(&q,2,5));
    for(unsigned i=0;i<256;i++)assert(xv_render_snapshots_add(&q,i,2,1,&p[0][0]));
    assert(!xv_render_snapshots_add(&q,256,2,1,&p[0][0]));
    assert(!xv_render_snapshots_publish(&q));
    assert(xv_render_snapshots_init(&q,&storage[0],&storage[1]));
    pthread_t a,b;assert(!pthread_create(&b,NULL,consumer,NULL));
    assert(!pthread_create(&a,NULL,producer,NULL));
    assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));assert(observed);
    printf("PASS owned poses, lifetime, world/object identity, complete publication, 10000 concurrent ticks (%u observed)\n",observed);
}
