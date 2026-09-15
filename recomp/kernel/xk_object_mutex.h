/* One recursive guard, with a drained-boundary Vita backend comparison.
 * No caller may switch backends while a worker or owner holds the guard. */
#pragma once
#include <stdlib.h>
#include <string.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr/mutex.h>
#include <psp2/kernel/threadmgr/lw_mutex.h>
#include <psp2/kernel/error.h>
#else
#include <pthread.h>
#include <errno.h>
#include <time.h>
#endif

typedef struct {
    int ready,light_ready,use_light,configured;
#ifdef __vita__
    SceUID full;
    SceKernelLwMutexWork light;
#else
    pthread_mutex_t full;
#endif
} xv_object_mutex;

static inline void xv_object_mutex_select(xv_object_mutex *m,int value)
{m->use_light=m->light_ready && (value<0?m->configured:!!value);}

static inline int xv_object_mutex_init(xv_object_mutex *m,int configured)
{
    memset(m,0,sizeof *m);m->configured=!!configured;
#ifdef __vita__
    m->full=sceKernelCreateMutex("xv_object_math",SCE_KERNEL_MUTEX_ATTR_RECURSIVE,0,NULL);
    if(m->full<0)return -1;
    m->light_ready=sceKernelCreateLwMutex(&m->light,"xv_object_math_lw",
        SCE_KERNEL_MUTEX_ATTR_RECURSIVE,0,NULL)==0;
#else
    pthread_mutexattr_t attr;
    if(pthread_mutexattr_init(&attr))return -1;
    int result=pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    if(!result)result=pthread_mutex_init(&m->full,&attr);
    pthread_mutexattr_destroy(&attr);
    if(result)return -1;
#endif
    m->ready=1;xv_object_mutex_select(m,-1);return 0;
}
static inline void xv_object_mutex_destroy(xv_object_mutex *m)
{
    if(!m->ready)return;
#ifdef __vita__
    if(m->light_ready && sceKernelDeleteLwMutex(&m->light))abort();
    if(sceKernelDeleteMutex(m->full))abort();
#else
    if(pthread_mutex_destroy(&m->full))abort();
#endif
    m->ready=m->light_ready=m->use_light=0;
}
/* Return 1 only for contention. All other errors are programming failures. */
static inline int xv_object_mutex_try(xv_object_mutex *m)
{
#ifdef __vita__
    int result=m->use_light?sceKernelTryLockLwMutex(&m->light,1):sceKernelTryLockMutex(m->full,1);
    int busy=m->use_light?(int)SCE_KERNEL_ERROR_LW_MUTEX_FAILED_TO_OWN:(int)SCE_KERNEL_ERROR_MUTEX_FAILED_TO_OWN;
#else
    int result=pthread_mutex_trylock(&m->full),busy=EBUSY;
#endif
    if(result==busy)return 1;
    if(result)abort();
    return 0;
}
static inline void xv_object_mutex_wait(xv_object_mutex *m)
{
#ifdef __vita__
    int result=m->use_light?sceKernelLockLwMutex(&m->light,1,NULL):sceKernelLockMutex(m->full,1,NULL);
#else
    int result=pthread_mutex_lock(&m->full);
#endif
    if(result)abort();
}
/* A worker must periodically acknowledge quiescent owner services even while
 * another worker retains this guard. Never use an infinite wait for that path.
 * Firmware may update timeout; give every attempt its own original budget. */
static inline int xv_object_mutex_wait_bounded(xv_object_mutex *m,unsigned timeout_us)
{
#ifdef __vita__
    unsigned timeout=timeout_us;
    int result=m->use_light?sceKernelLockLwMutex(&m->light,1,&timeout):sceKernelLockMutex(m->full,1,&timeout);
    if(result==(int)SCE_KERNEL_ERROR_WAIT_TIMEOUT)return 1;
#else
    struct timespec deadline;
    if(clock_gettime(CLOCK_REALTIME,&deadline))abort();
    deadline.tv_sec+=timeout_us/1000000u;
    deadline.tv_nsec+=(long)(timeout_us%1000000u)*1000;
    if(deadline.tv_nsec>=1000000000L) {deadline.tv_nsec-=1000000000L;deadline.tv_sec++;}
    int result=pthread_mutex_timedlock(&m->full,&deadline);
    if(result==ETIMEDOUT)return 1;
#endif
    if(result)abort();
    return 0;
}
static inline void xv_object_mutex_release(xv_object_mutex *m)
{
#ifdef __vita__
    int result=m->use_light?sceKernelUnlockLwMutex(&m->light,1):sceKernelUnlockMutex(m->full,1);
#else
    int result=pthread_mutex_unlock(&m->full);
#endif
    if(result)abort();
}
static inline const char *xv_object_mutex_name(const xv_object_mutex *m)
{
#ifdef __vita__
    return m->use_light?"light":"kernel";
#else
    (void)m;return "pthread";
#endif
}
