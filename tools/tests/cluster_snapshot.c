#include "kernel/xk_cluster_snapshot.h"
#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BASE=0x10000000, COLL=0x200, PROJECTION=0x260, PLANES=0x300,
       PLANES2=0x400, CLUSTERS=0x500, PORTALS=0x800, ADJ=0x900,
       VERTICES=0xa00, AXES=0xd00, ZERO=0xd40 };
typedef struct {unsigned char bytes[4096];unsigned calls,fail_at;} Reader;
static Reader reader;
static XvClusterSource source={BASE,BASE+PROJECTION,BASE+AXES,BASE+ZERO};
static unsigned allocations,fail_allocation,live;
void *__real_calloc(size_t,size_t);
void __real_free(void *);
void *__wrap_calloc(size_t n,size_t size)
{
    if(++allocations==fail_allocation)return NULL;
    void *p=__real_calloc(n,size);
    if(p)__atomic_add_fetch(&live,1,__ATOMIC_RELAXED);
    return p;
}
void __wrap_free(void *p)
{
    if(p){assert(__atomic_load_n(&live,__ATOMIC_RELAXED));__atomic_sub_fetch(&live,1,__ATOMIC_RELAXED);}
    __real_free(p);
}
static int read_bytes(void *context,uint32_t address,void *out,size_t size)
{
    Reader *r=context;
    if(++r->calls==r->fail_at)return 0;
    if(address<BASE||address-BASE>sizeof r->bytes||size>sizeof r->bytes-(address-BASE))return 0;
    memcpy(out,r->bytes+address-BASE,size);return 1;
}
static void put(unsigned offset,const void *data,size_t size)
{assert(offset+size<=sizeof reader.bytes);memcpy(reader.bytes+offset,data,size);}
static void word(unsigned offset,uint32_t value){put(offset,&value,4);}
static void setup(void)
{
    memset(&reader,0,sizeof reader);allocations=fail_allocation=0;
    word(0x134,3);word(0x138,BASE+CLUSTERS);word(0x154,2);word(0x158,BASE+PORTALS);
    word(0xb0,1);word(0xb4,BASE+COLL);
    word(COLL+0xc,4);word(COLL+0x10,BASE+PLANES);
    word(PROJECTION+0xc,4);word(PROJECTION+0x10,BASE+PLANES2);
    for(unsigned i=0;i<4;i++){
        float p[4]={1,0,0,(float)i*.125f};put(PLANES+i*16,p,16);
        p[3]*=-1;put(PLANES2+i*16,p,16);
    }
    for(unsigned i=0;i<6;i++){int16_t axes[2]={1,2};put(AXES+i*4,axes,4);}
    for(unsigned i=0;i<3;i++){
        word(CLUSTERS+i*104+0x5c,i==1?2:1);word(CLUSTERS+i*104+0x60,BASE+ADJ+i*8);
        uint16_t a[2]={i==2?1:0,1};put(ADJ+i*8,a,4);
    }
    for(unsigned i=0;i<2;i++){
        int16_t sides[2]={(int16_t)i,(int16_t)(i+1)};put(PORTALS+i*64,sides,4);
        word(PORTALS+i*64+4,i?1:3);
        float center[4]={0,0,0,4};put(PORTALS+i*64+8,center,16);
        word(PORTALS+i*64+0x34,4);word(PORTALS+i*64+0x38,BASE+VERTICES+i*48);
        float vertices[4][3]={{0,-2,-2},{0,2,-2},{0,2,2},{0,-2,2}};
        put(VERTICES+i*48,vertices,sizeof vertices);
    }
}
static XvClusterSnapshot *build(size_t limit)
{reader.calls=0;return xv_cluster_snapshot_build(read_bytes,&reader,&source,limit);}
static void query(const XvClusterSnapshot *snapshot)
{
    uint32_t visited[256]={0};XvClusterResult result;
    XvClusterInput input={.radius=10,.start=0,.epoch=100,.budget=10000,.visited=visited};
    assert(xv_cluster_query_direct(xv_cluster_snapshot_geometry(snapshot),&input,&result));
    assert(result.count==3&&result.clusters[0]==0&&result.clusters[1]==1&&result.clusters[2]==2);
}
static void constructor(void)
{
    setup();unsigned char before[4096];memcpy(before,reader.bytes,sizeof before);
    XvClusterSnapshot *s=build(65536);assert(s);query(s);
    unsigned reads=reader.calls;size_t bytes=xv_cluster_snapshot_bytes(s);
    assert(bytes<2048&&allocations==2&&!memcmp(before,reader.bytes,sizeof before));
    const XvClusterGeometry *g=xv_cluster_snapshot_geometry(s);
    assert(g->distance_plane_count==2&&g->projection_plane_count==2);
    assert(g->distance_planes[g->portals[0].plane].v[3]==.375f);
    assert(g->projection_planes[g->portals[0].plane].v[3]==-.375f);
    xv_cluster_snapshot_release(s);assert(!live);
    s=build(bytes);assert(s&&xv_cluster_snapshot_bytes(s)==bytes);xv_cluster_snapshot_release(s);
    assert(!build(bytes-1)&&!live);
    for(unsigned i=1;i<=reads;i++){
        setup();reader.fail_at=i;assert(!build(65536)&&!live);
    }
    for(unsigned i=1;i<=2;i++){
        setup();fail_allocation=i;assert(!build(65536)&&!live);
    }
    struct {unsigned offset;uint32_t value;} bad[]={
        {0x134,257},{0x154,32769},{CLUSTERS+0x5c,32768},
        {PORTALS+0x34,129},{PORTALS,0xffffffff},{PORTALS+4,4},
        {AXES,3},{ZERO,1},{CLUSTERS+0x60,UINT32_MAX},
        {PORTALS+0x38,UINT32_MAX},{COLL+0x10,UINT32_MAX},
        {PROJECTION+0x10,UINT32_MAX},{PORTALS+20,0x7f800000},
    };
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){
        setup();word(bad[i].offset,bad[i].value);assert(!build(65536)&&!live);
    }
    setup();XvClusterSource invalid=source;invalid.bsp=UINT32_MAX-4;
    assert(!xv_cluster_snapshot_build(read_bytes,&reader,&invalid,65536)&&!live);
    setup();word(COLL+0xc,UINT32_MAX);word(PROJECTION+0xc,UINT32_MAX);
    word(PORTALS+4,UINT32_MAX-1);assert(!build(65536)&&!live);
    /* Single cluster, no adjacency/portals/vertices/planes: an owned empty query. */
    setup();word(0x134,1);word(0x154,0);word(CLUSTERS+0x5c,0);
    s=build(65536);assert(s);bytes=xv_cluster_snapshot_bytes(s);xv_cluster_snapshot_release(s);
    assert(!build(bytes-1)&&!live);s=build(bytes);assert(s);xv_cluster_snapshot_release(s);
    printf("PASS snapshot construction: independent planes, bounds, %u read failures, allocation failures, exact limit and empty graph\n",reads);
}
static void retirement(void)
{
    XvClusterStore store={0};setup();XvClusterSnapshot *s=build(65536);assert(s);
    assert(xv_cluster_store_publish(&store,s));
    XvClusterLease a=xv_cluster_store_acquire(&store),b=xv_cluster_store_acquire(&store);
    assert(a.snapshot&&b.snapshot&&xv_cluster_store_current(&store,&a));
    xv_cluster_store_retire(&store);
    assert(!xv_cluster_store_current(&store,&a)&&!xv_cluster_store_acquire(&store).snapshot);
    memset(reader.bytes,0xee,sizeof reader.bytes);query(a.snapshot);query(b.snapshot);
    xv_cluster_snapshot_release(a.snapshot);
    assert(!xv_cluster_store_publish(&store,b.snapshot)); /* retired snapshots cannot be republished */
    setup();s=build(65536);assert(s&&xv_cluster_store_publish(&store,s));
    assert(!xv_cluster_store_current(&store,&b));query(b.snapshot);
    xv_cluster_snapshot_release(b.snapshot);xv_cluster_store_retire(&store);assert(!live);
    setup();s=build(65536);assert(s);store.generation=UINT64_MAX;
    assert(!xv_cluster_store_publish(&store,s));xv_cluster_snapshot_release(s);
    xv_cluster_store_retire(&store);assert(store.generation==UINT64_MAX&&!live);
    puts("PASS snapshot retirement: outstanding readers survive source overwrite, stale generations decline, no republishing or generation wrap");
}
static pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
static XvClusterStore shared;
static unsigned done;
static void *worker(void *unused)
{
    (void)unused;unsigned queries=0;
    while(!__atomic_load_n(&done,__ATOMIC_ACQUIRE)||queries<100){
        pthread_mutex_lock(&mutex);XvClusterLease lease=xv_cluster_store_acquire(&shared);pthread_mutex_unlock(&mutex);
        if(!lease.snapshot){sched_yield();continue;}
        query(lease.snapshot);queries++;
        pthread_mutex_lock(&mutex);
        (void)xv_cluster_store_current(&shared,&lease); /* stale results would fall back */
        xv_cluster_snapshot_release(lease.snapshot);pthread_mutex_unlock(&mutex);
    }
    return NULL;
}
static void concurrent(void)
{
    setup();XvClusterSnapshot *s=build(65536);assert(s&&xv_cluster_store_publish(&shared,s));
    pthread_t workers[2];
    for(unsigned i=0;i<2;i++)assert(!pthread_create(&workers[i],NULL,worker,NULL));
    for(unsigned i=0;i<2000;i++){
        s=build(65536);assert(s);
        pthread_mutex_lock(&mutex);
        if(i%3==0)xv_cluster_store_retire(&shared);
        assert(xv_cluster_store_publish(&shared,s));pthread_mutex_unlock(&mutex);
    }
    __atomic_store_n(&done,1,__ATOMIC_RELEASE);
    for(unsigned i=0;i<2;i++)assert(!pthread_join(workers[i],NULL));
    xv_cluster_store_retire(&shared);assert(!live);
    puts("PASS snapshot concurrency: two readers, 2000 publications, retirement and final drain");
}
int main(void){constructor();retirement();concurrent();return 0;}
