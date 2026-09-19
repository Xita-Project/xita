#include <assert.h>
#include <stdarg.h>
#include <sched.h>
#include <unistd.h>
#include "../../recomp/kernel/xk_pose_pipeline.c"
void xv_logf(const char *fmt,...) {(void)fmt;}
static unsigned pause_work,entered;
static void multiply(const float *a,const float *b,float *o)
{
    __atomic_store_n(&entered,1,__ATOMIC_RELEASE);
    while(__atomic_load_n(&pause_work,__ATOMIC_ACQUIRE))sched_yield();
    for(unsigned i=0;i<13;i++)o[i]=a[i]+b[i];
}
static void complete(void)
{
    for(unsigned i=0;i<100000&&LOAD(pending);i++)usleep(10);
    assert(!LOAD(pending));
}
static float a[64][13],b[64][39],out[64][13];
static void input(float v)
{for(unsigned n=0;n<64;n++)for(unsigned i=0;i<13;i++){a[n][i]=v+n+i;b[n][i]=2;}}
static int attempt(unsigned datum,unsigned model,unsigned count)
{return xv_pose_pipeline_try(datum,model,0x3000,0x4000,count,&a[0][0],b,156,multiply,&out[0][0]);}
int main(void)
{
    input(1);memset(out,0x5a,sizeof out);
    xv_pose_pipeline_begin(1);assert(!attempt(0x10001,7,64));
    assert(*(unsigned char *)out==0x5a);
    xv_pose_pipeline_end();complete();
    input(100);xv_pose_pipeline_begin(2);assert(attempt(0x10001,7,64));
    for(unsigned n=0;n<64;n++)for(unsigned i=0;i<13;i++)assert(out[n][i]==3+n+i);
    /* Duplicate unchanged pass is allowed; changed data in the same frame
     * declines, preventing two different definitions of a captured object. */
    assert(attempt(0x10001,7,64));a[0][0]=999;assert(!attempt(0x10001,7,64));
    a[0][0]=100;
    xv_pose_pipeline_end();memset(a,0,sizeof a);complete();
    xv_pose_pipeline_begin(3);input(200);assert(attempt(0x10001,7,64));
    assert(out[0][0]==102);assert(!attempt(0x20001,7,64));
    xv_pose_pipeline_end();complete();
    xv_pose_pipeline_begin(4);input(300);assert(!attempt(0x10001,8,64));
    xv_pose_pipeline_end();complete();
    xv_pose_pipeline_begin(5);b[0][0]=3;assert(!attempt(0x10001,8,64));
    xv_pose_pipeline_end();complete();
    xv_pose_pipeline_invalidate();xv_pose_pipeline_begin(6);
    assert(!attempt(0x10001,8,64));xv_pose_pipeline_end();complete();
    /* A late worker cannot be waited on, overwritten, or published as current. */
    STORE(pause_work,1);STORE(entered,0);xv_pose_pipeline_begin(7);attempt(0x10001,8,64);
    xv_pose_pipeline_end();
    while(!LOAD(entered))sched_yield();
    xv_pose_pipeline_begin(8);assert(!attempt(0x10001,8,64));
    xv_pose_pipeline_end();assert(LOAD(pending));
    STORE(pause_work,0);complete();
    xv_pose_pipeline_begin(9);assert(!attempt(0x10001,8,64));
    xv_pose_pipeline_end();complete();
    for(unsigned f=10;f<1010;f++) {
        xv_pose_pipeline_begin(f);input((float)f);
        for(unsigned n=0;n<20;n++)attempt(0x30000+n,7,1+(n%64));
        xv_pose_pipeline_end();if(!(f%3))sched_yield();
    }
    xv_pose_pipeline_shutdown();assert(!inputs&&!results&&!initialized);
    xv_pose_pipeline_begin(1);assert(!attempt(0x10001,7,1));
    xv_pose_pipeline_end();complete();xv_pose_pipeline_shutdown();
    puts("PASS async owned palettes: previous complete pose, changed keys/bindings, late worker, world invalidation, input mutation, stress and shutdown/restart");
}
