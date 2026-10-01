/* Deliberately experimental parallel object updates; ordinary builds omit it. */
#pragma once
#include "../xv_x86rt.h"

#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
enum { XV_OBJECT_JOB_STACK_BYTES=256*1024 };
extern const char xv_object_job_marker;
static inline int xv_is_object_job(const xctx *c)
{ return c && c->fiber == &xv_object_job_marker; }
int xv_object_jobs_begin(xctx *c);
int xv_object_feature_serial_admit(const xctx *c);
int xv_object_jobs_queue(xctx *c);
void xv_object_jobs_join(void);
void xv_object_jobs_end(xctx **owner);
void xv_object_jobs_finish(xctx *owner);
void xv_object_jobs_override(int enabled);
void xv_object_jobs_shutdown(void);
void xv_object_jobs_report(unsigned frames);
void xv_object_job_hle(xctx *c, unsigned address, xv_fn_t fn);
void xv_object_job_stack_probe(xctx *c);
void xv_object_job_stop(xctx *c, unsigned address, const char *reason) __attribute__((noreturn));
/* Actual native-thread identity only; false is not proof of guest ownership. */
int xv_object_is_worker_thread(void);
int xv_object_math_lock(void);
/* Scene abandonment only: caller must immediately discard all guard tokens. */
unsigned xv_object_math_abandon_current(void);
#if XV_QUERY_WORLD_RUN
/* Read-only admission: exact worker guest under the retained math transaction. */
unsigned xv_object_world_run_admit(xctx *c);
void xv_query_world_run_report(unsigned frames);
#endif
void xv_object_math_unlock(int *locked);
/* Drained-owner diagnostic only; ordinary builds have no hold instrumentation. */
int xv_object_holds_available(void);
int xv_object_holds_enabled(void);
void xv_object_holds_override(int enabled);
/* Inputs and shared bookkeeping must be captured before this call. Only the
 * current lane's unchanged private stack mappings may be written afterwards. */
int xv_object_math_release_private(xctx *c,int *locked,unsigned kind,
    uint32_t output,unsigned output_bytes,uint32_t scratch,unsigned scratch_bytes);
int xv_object_math_available(void);
int xv_object_lock_available(void);
void xv_object_lock_override(int enabled);
int xv_object_private_point(xctx *c);
int xv_object_private_quaternion(xctx *c);
int xv_object_quat_available(void);
void xv_object_quat_override(int enabled);
void xv_object_math_report_check(void);
int xv_object_point_available(void);
void xv_object_point_override(int enabled);
int xv_object_wait_available(void);
void xv_object_wait_override(int enabled);
void xv_object_math_override(int enabled);
#ifdef XV_OBJECT_POSE_EXPERIMENT
/* Owner selects only after jobs drain. Negative restores the disabled default.
 * Tokens belong to one native worker and are never stored in guest memory. */
int xv_object_pose_available(void);
void xv_object_pose_override(int enabled);
int xv_object_pose_begin(xctx *c);
void xv_object_pose_finish(int *token);
void xv_object_pose_cleanup(int *token);
#define XV_OBJECT_POSE_SCOPE() \
    int xv_object_pose_locked_ __attribute__((cleanup(xv_object_pose_cleanup))) = -1
#define XV_OBJECT_POSE_BEGIN(c) do { \
    if (xv_object_pose_locked_ < 0) xv_object_pose_locked_ = xv_object_pose_begin(c); \
} while (0)
#define XV_OBJECT_POSE_FINISH() xv_object_pose_finish(&xv_object_pose_locked_)
#endif
#define XV_OBJECT_JOB_SCOPE(c) \
    xctx *xv_object_owner_ __attribute__((cleanup(xv_object_jobs_end))) = \
        xv_object_jobs_begin(c) ? (c) : NULL
#ifdef XV_LIGHT_QUERY_CENSUS
#include "xk_light_census.h"
/* Logical scope tracking includes the idle fast path. It does not acquire a
 * mutex or change lock tokens, recursion, owner services or the allowlist. */
int xv_object_census_scope_begin(void);
void xv_object_census_scope_end(int *);
static inline void xv_object_census_scope_cleanup(int *token)
{if(*token)xv_object_census_scope_end(token);}
#define XV_OBJECT_CENSUS_SCOPE() \
    int xv_object_census_scoped_ __attribute__((cleanup(xv_object_census_scope_cleanup))) = (XV_LIGHT_CENSUS_ON() ? xv_object_census_scope_begin() : 0)
#else
#define XV_OBJECT_CENSUS_SCOPE() ((void)0)
#endif
#define XV_OBJECT_MATH_GUARD() \
    XV_OBJECT_CENSUS_SCOPE(); \
    int xv_object_math_locked_ __attribute__((cleanup(xv_object_math_unlock))) = xv_object_math_lock()
#define XV_OBJECT_MATH_PRIVATE(c,kind,out,bytes,scratch,scratch_bytes) \
    ((void)xv_object_math_release_private(c,&xv_object_math_locked_,kind,out,bytes,scratch,scratch_bytes))
#else
#define XV_OBJECT_JOB_SCOPE(c) ((void)0)
#define XV_OBJECT_MATH_GUARD() ((void)0)
#define XV_OBJECT_MATH_PRIVATE(c,kind,out,bytes,scratch,scratch_bytes) ((void)0)
#endif
