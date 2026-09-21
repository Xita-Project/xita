/* Control/statistics only. The owned-game routine is generated separately. */
#include "xk_polygon_edge.h"
int xv_owner_thread_id(void);   /* xk_os_vita.c: owner alias for the scene helper */
#include <limits.h>
#include <stdlib.h>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
static SceUID owner_thread;
static int is_owner(void) { return xv_owner_thread_id() == owner_thread; }
static void bind_owner(void) { owner_thread = sceKernelGetThreadId(); }
#else
#include <pthread.h>
pthread_t xv_owner_pthread_self(void);   /* xk_scene_thread.c: owner alias for the scene helper (host) */
static pthread_t owner_thread;
static int is_owner(void) { return pthread_equal(xv_owner_pthread_self(), owner_thread); }
static void bind_owner(void) { owner_thread = xv_owner_pthread_self(); }
#endif

extern void xv_object_math_report_check(void) __attribute__((weak));
/* 0 uninitialized, 1 binding, 2 ready. Published owner is immutable. */
static unsigned ready;
/* Bit 0 enables admissions, upper bits count active helpers, including yields.
 * Combining the fields makes admission vs. mode change a single atomic order. */
static unsigned state;
static unsigned calls;

static void owner_drained(void)
{
    if (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) != 2 || !is_owner()) abort();
    if (xv_object_math_report_check) xv_object_math_report_check();
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) >> 1) abort();
}

void xv_native_polygon_edge_init(void)
{
    unsigned expected = 0;
    if (__atomic_compare_exchange_n(&ready, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        /* Integration calls this at a joined/idle owner boundary. No helper
         * can be active yet: state stays zero until that owner enables it. */
        if (xv_object_math_report_check) xv_object_math_report_check();
        bind_owner();
        __atomic_store_n(&ready, 2, __ATOMIC_RELEASE);
    } else {
        owner_drained();
    }
}

int xv_native_polygon_edge_available(void)
{
    return __atomic_load_n(&ready, __ATOMIC_ACQUIRE) == 2;
}

void xv_native_polygon_edge_override(int enabled)
{
    owner_drained();
    unsigned expected = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
    if ((expected >> 1) ||
        !__atomic_compare_exchange_n(&state, &expected, enabled > 0 ? 1u : 0u, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) abort();
}

unsigned xv_math_polygon_edge_calls(void)
{
    owner_drained();
    /* Modulo unsigned, like the other interval counters, without lost updates. */
    return __atomic_exchange_n(&calls, 0, __ATOMIC_RELAXED);
}

int xv_polygon_edge_begin(void)
{
    unsigned old = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
    for (;;) {
        if (!(old & 1u)) return 0;
        if (old > UINT_MAX - 2u) abort();
        if (__atomic_compare_exchange_n(&state, &old, old + 2u, 1,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
            __atomic_fetch_add(&calls, 1, __ATOMIC_RELAXED);
            return 1;
        }
    }
}

void xv_polygon_edge_end(void)
{
    if ((__atomic_fetch_sub(&state, 2, __ATOMIC_RELEASE) >> 1) == 0) abort();
}
