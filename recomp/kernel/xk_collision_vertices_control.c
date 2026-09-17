#ifdef XV_NATIVE_COLLISION_VERTICES
#include "xk_collision_vertices.h"
#if defined(__vita__)
#include <psp2/kernel/threadmgr.h>
static SceUID owner_thread;
static void bind_owner(void){owner_thread=sceKernelGetThreadId();}
static int is_owner(void){return owner_thread==sceKernelGetThreadId();}
#elif defined(TEST_ARM)
/* The instruction fixture is single-threaded. Host tests exercise identity. */
static void bind_owner(void){}
static int is_owner(void){return 1;}
#else
#include <pthread.h>
static pthread_t owner_thread;
static void bind_owner(void){owner_thread=pthread_self();}
static int is_owner(void){return pthread_equal(owner_thread,pthread_self());}
#endif
extern void xv_object_math_report_check(void) __attribute__((weak));
#ifndef XV_NATIVE_COLLISION_VERTICES_DEFAULT
#define XV_NATIVE_COLLISION_VERTICES_DEFAULT 0
#endif
#if XV_NATIVE_COLLISION_VERTICES_DEFAULT != 0 && XV_NATIVE_COLLISION_VERTICES_DEFAULT != 1
#error XV_NATIVE_COLLISION_VERTICES_DEFAULT must be 0 or 1
#endif
/* Process-start selection precedes all guest/worker threads. No live control
 * binding is needed for ordinary admission and cleanup. */
unsigned xv_collision_vertices_state=XV_NATIVE_COLLISION_VERTICES_DEFAULT;
unsigned xv_collision_vertices_count;
static unsigned ready;
static void drained(void)
{
    if(__atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=2||!is_owner())abort();
    if(xv_object_math_report_check)xv_object_math_report_check();
    if(__atomic_load_n(&xv_collision_vertices_state,__ATOMIC_ACQUIRE)>>1)abort();
}
void xv_collision_vertices_init(void)
{
    unsigned expected=0;
    if(__atomic_compare_exchange_n(&ready,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        if(xv_object_math_report_check)xv_object_math_report_check();
        bind_owner();__atomic_store_n(&ready,2,__ATOMIC_RELEASE);
    }else drained();
}
int xv_collision_vertices_available(void)
{return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)==2;}
int xv_collision_vertices_enabled(void)
{return !!(__atomic_load_n(&xv_collision_vertices_state,__ATOMIC_ACQUIRE)&1u);}
int xv_collision_vertices_control_ready(void)
{return xv_collision_vertices_available()&&is_owner()&&
        !(__atomic_load_n(&xv_collision_vertices_state,__ATOMIC_ACQUIRE)>>1);}
void xv_collision_vertices_override(int enabled)
{
    drained();unsigned old=__atomic_load_n(&xv_collision_vertices_state,__ATOMIC_ACQUIRE);
    if((old>>1)||!__atomic_compare_exchange_n(&xv_collision_vertices_state,&old,enabled>0?1u:0u,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))abort();
}
unsigned xv_collision_vertices_calls(void)
{drained();return __atomic_exchange_n(&xv_collision_vertices_count,0,__ATOMIC_RELAXED);}
#endif
