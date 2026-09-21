/* Control/statistics only. The owned-game routine is generated separately. */
#include "xk_clip_region.h"
int xv_owner_thread_id(void);   /* xk_os_vita.c: owner alias for the scene helper */
#ifdef XV_LIGHT_QUERY_CENSUS
#include "xk_light_census.h"
#endif
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
static xv_clip_region_work totals;

static void owner_drained(void)
{
    if (__atomic_load_n(&ready, __ATOMIC_ACQUIRE) != 2 || !is_owner()) abort();
    if (xv_object_math_report_check) xv_object_math_report_check();
    if (__atomic_load_n(&state, __ATOMIC_ACQUIRE) >> 1) abort();
}

void xv_native_clip_region_init(void)
{
    unsigned expected = 0;
    if (__atomic_compare_exchange_n(&ready, &expected, 1, 0,
                                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        /* Integration calls this at a joined/idle owner boundary. No helper
         * can be active yet: state stays zero until that owner enables it. */
        if (xv_object_math_report_check) xv_object_math_report_check();
        (void)xv_clip_region_compatible();
        bind_owner();
        __atomic_store_n(&ready, 2, __ATOMIC_RELEASE);
    } else {
        owner_drained();
    }
}

int xv_native_clip_region_available(void)
{
    return __atomic_load_n(&ready, __ATOMIC_ACQUIRE) == 2;
}

int xv_native_clip_region_enabled(void)
{ return !!(__atomic_load_n(&state, __ATOMIC_ACQUIRE) & 1u); }

void xv_native_clip_region_override(int enabled)
{
    owner_drained();
    unsigned expected = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
    if ((expected >> 1) ||
        !__atomic_compare_exchange_n(&state, &expected, enabled > 0 ? 1u : 0u, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) abort();
}

void xv_clip_region_read_work(xv_clip_region_work *out)
{
    owner_drained();
    /* All writers retire before the owner's drain. Atomic additions avoid
     * lost completion records without a per-call native thread lookup. */
    *out = totals;
    totals = (xv_clip_region_work){0};
}

extern void xk_os_log(const char *, ...) __attribute__((weak));
void xv_clip_region_report(unsigned frames)
{
    if (!xv_native_clip_region_available()) return;
    xv_clip_region_work n;
    xv_clip_region_read_work(&n);
    if (xk_os_log) xk_os_log("[clip-region] %u frames: regions %u planes %u clips %u input-vertices %u capacity-failures %u max-clips %u\n",
        frames,n.regions,n.planes,n.clips,n.input_vertices,n.capacity_failures,n.max_clips);
#if XV_TYPED_PORTAL_POLYGON
    extern void xv_portal_polygon_report(unsigned);
    xv_portal_polygon_report(frames);
#endif
#if XV_TYPED_SUBCLUSTER
    extern void xv_subcluster_report(unsigned);
    xv_subcluster_report(frames);
#endif
#if XV_NATIVE_VISIBILITY_PASS
    extern void xv_visibility_pass_report(unsigned);
    xv_visibility_pass_report(frames);
#endif
}

int xv_clip_region_begin(void)
{
    unsigned old = __atomic_load_n(&state, __ATOMIC_ACQUIRE);
    for (;;) {
        if (!(old & 1u)) return 0;
#ifdef XV_LIGHT_QUERY_CENSUS
        /* The original native clip owns census logical-scope tracking. Until
         * fusion reproduces that scope, keep its original guarded calls. */
        if (XV_LIGHT_CENSUS_ON()) return 0;
#endif
        if (!xv_clip_region_compatible()) return 0;
        if (old > UINT_MAX - 2u) abort();
        if (__atomic_compare_exchange_n(&state, &old, old + 2u, 1,
                                        __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {

            return 1;
        }
    }
}

void xv_clip_region_end(const xv_clip_region_work *work)
{
    if (work) {
#define ADD(field) __atomic_fetch_add(&totals.field,work->field,__ATOMIC_RELAXED)
        ADD(regions); ADD(planes); ADD(clips); ADD(input_vertices); ADD(capacity_failures);
#undef ADD
        unsigned old=__atomic_load_n(&totals.max_clips,__ATOMIC_RELAXED);
        while (old < work->clips && !__atomic_compare_exchange_n(&totals.max_clips,
               &old,work->clips,1,__ATOMIC_RELAXED,__ATOMIC_RELAXED)) {}
    }
    if ((__atomic_fetch_sub(&state, 2, __ATOMIC_RELEASE) >> 1) == 0) abort();
}
