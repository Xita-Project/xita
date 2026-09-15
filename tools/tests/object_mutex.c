#define _POSIX_C_SOURCE 200809L
#include "kernel/xk_object_mutex.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

/* Exercise the production Vita adapter with recursive host primitives. This
 * checks API selection/lifetime/error handling, not firmware lock performance. */
static pthread_mutex_t locks[2];
static SceKernelLwMutexWork *work;
static int fail_full,fail_light,fault;
static unsigned created[2],deleted[2],tries[2],waits[2],releases[2],value;
static xv_object_mutex tested;
static void init(unsigned i)
{
    pthread_mutexattr_t attr;assert(!pthread_mutexattr_init(&attr));
    assert(!pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&locks[i],&attr));pthread_mutexattr_destroy(&attr);created[i]++;
}
SceUID sceKernelCreateMutex(const char *name,unsigned attr,int count,SceKernelMutexOptParam *opt)
{assert(name&&attr==SCE_KERNEL_MUTEX_ATTR_RECURSIVE&&!count&&!opt);if(fail_full)return -2;init(0);return 17;}
int sceKernelCreateLwMutex(SceKernelLwMutexWork *p,const char *name,unsigned attr,int count,const SceKernelLwMutexOptParam *opt)
{assert(name&&attr==SCE_KERNEL_MUTEX_ATTR_RECURSIVE&&!count&&!opt);if(fail_light)return -3;work=p;init(1);return 0;}
int sceKernelDeleteMutex(SceUID id)
{assert(id==17);deleted[0]++;return pthread_mutex_destroy(&locks[0]);}
int sceKernelDeleteLwMutex(SceKernelLwMutexWork *p)
{assert(p==work);deleted[1]++;return pthread_mutex_destroy(&locks[1]);}
static int attempt(unsigned i)
{
    __atomic_add_fetch(&tries[i],1,__ATOMIC_RELAXED);
    if(fault==1)return -1;
    int r=pthread_mutex_trylock(&locks[i]);
    if(r==EBUSY)return i?(int)SCE_KERNEL_ERROR_LW_MUTEX_FAILED_TO_OWN:(int)SCE_KERNEL_ERROR_MUTEX_FAILED_TO_OWN;
    assert(!r);return 0;
}
int sceKernelTryLockMutex(SceUID id,int count) {assert(id==17&&count==1);return attempt(0);}
int sceKernelTryLockLwMutex(SceKernelLwMutexWork *p,int count) {assert(p==work&&count==1);return attempt(1);}
static int wait_lock(unsigned i)
{__atomic_add_fetch(&waits[i],1,__ATOMIC_RELAXED);return fault==2?-1:pthread_mutex_lock(&locks[i]);}
int sceKernelLockMutex(SceUID id,int count,unsigned *timeout)
{assert(id==17&&count==1&&!timeout);return wait_lock(0);}
int sceKernelLockLwMutex(SceKernelLwMutexWork *p,int count,unsigned *timeout)
{assert(p==work&&count==1&&!timeout);return wait_lock(1);}
static int release(unsigned i)
{__atomic_add_fetch(&releases[i],1,__ATOMIC_RELAXED);return fault==3?-1:pthread_mutex_unlock(&locks[i]);}
int sceKernelUnlockMutex(SceUID id,int count) {assert(id==17&&count==1);return release(0);}
int sceKernelUnlockLwMutex(SceKernelLwMutexWork *p,int count) {assert(p==work&&count==1);return release(1);}
static void *contend(void *unused)
{(void)unused;assert(xv_object_mutex_try(&tested)==1);return NULL;}
static void *increment(void *unused)
{
    (void)unused;
    for(unsigned i=0;i<1000;i++) {
        xv_object_mutex_wait(&tested);assert(!xv_object_mutex_try(&tested));
        value++;xv_object_mutex_release(&tested);xv_object_mutex_release(&tested);
    }
    return NULL;
}
int main(int argc,char **argv)
{
    if(argc>1) {
        assert(!xv_object_mutex_init(&tested,atoi(argv[1])));
        fault=atoi(argv[2]);
        if(fault==1)xv_object_mutex_try(&tested);
        if(fault==2)xv_object_mutex_wait(&tested);
        if(fault==3)xv_object_mutex_release(&tested);
        assert(0);
    }
    fail_full=1;assert(xv_object_mutex_init(&tested,1)==-1);
    xv_object_mutex_destroy(&tested);assert(!created[0]&&!created[1]);fail_full=0;
    fail_light=1;assert(!xv_object_mutex_init(&tested,1));
    assert(tested.ready&&!tested.light_ready&&!tested.use_light);
    xv_object_mutex_select(&tested,1);assert(!tested.use_light);
    assert(!xv_object_mutex_try(&tested));xv_object_mutex_release(&tested);
    xv_object_mutex_destroy(&tested);assert(deleted[0]==1&&!deleted[1]);fail_light=0;
    for(unsigned configured=0;configured<2;configured++) {
        assert(!xv_object_mutex_init(&tested,configured));
        int modes[]={-1,1,0,-1};
        for(unsigned i=0;i<4;i++) {
            xv_object_mutex_select(&tested,modes[i]);
            unsigned selected=modes[i]<0?configured:(unsigned)modes[i];
            assert(tested.use_light==(int)selected);
            assert(!strcmp(xv_object_mutex_name(&tested),selected?"light":"kernel"));
            unsigned previous_tries[2]={tries[0],tries[1]};
            assert(!xv_object_mutex_try(&tested));assert(!xv_object_mutex_try(&tested));
            pthread_t other;assert(!pthread_create(&other,NULL,contend,NULL));assert(!pthread_join(other,NULL));
            xv_object_mutex_release(&tested);xv_object_mutex_release(&tested);
            assert(tries[selected]==previous_tries[selected]+3&&tries[!selected]==previous_tries[!selected]);
            value=0;pthread_t a,b;
            assert(!pthread_create(&a,NULL,increment,NULL));assert(!pthread_create(&b,NULL,increment,NULL));
            assert(!pthread_join(a,NULL));assert(!pthread_join(b,NULL));assert(value==2000);
        }
        xv_object_mutex_destroy(&tested);xv_object_mutex_destroy(&tested);
        assert(deleted[0]==created[0]&&deleted[1]==created[1]);
    }
    puts("PASS: Vita mutex adapter recursion, contention codes, 16000 protected updates, backend isolation/restoration, optional creation failure and exact destruction");
}
