/* Deliberately experimental parallel object updates; ordinary builds omit it. */
#pragma once
#include "../xv_x86rt.h"

#ifdef XV_EXPERIMENTAL_OBJECT_JOBS
enum { XV_OBJECT_JOB_STACK_BYTES=256*1024 };
extern const char xv_object_job_marker;
static inline int xv_is_object_job(const xctx *c)
{ return c && c->fiber == &xv_object_job_marker; }
int xv_object_jobs_begin(xctx *c);
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
int xv_object_math_lock(void);
void xv_object_math_unlock(int *locked);
/* Inputs and shared bookkeeping must be captured before this call. Only the
 * current lane's unchanged private stack mappings may be written afterwards. */
int xv_object_math_release_private(xctx *c,int *locked,unsigned kind,
    uint32_t output,unsigned output_bytes,uint32_t scratch,unsigned scratch_bytes);
int xv_object_math_available(void);
int xv_object_lock_available(void);
void xv_object_lock_override(int enabled);
void xv_object_math_override(int enabled);
#define XV_OBJECT_JOB_SCOPE(c) \
    xctx *xv_object_owner_ __attribute__((cleanup(xv_object_jobs_end))) = \
        xv_object_jobs_begin(c) ? (c) : NULL
#define XV_OBJECT_MATH_GUARD() \
    int xv_object_math_locked_ __attribute__((cleanup(xv_object_math_unlock))) = xv_object_math_lock()
#define XV_OBJECT_MATH_PRIVATE(c,kind,out,bytes,scratch,scratch_bytes) \
    ((void)xv_object_math_release_private(c,&xv_object_math_locked_,kind,out,bytes,scratch,scratch_bytes))
#else
#define XV_OBJECT_JOB_SCOPE(c) ((void)0)
#define XV_OBJECT_MATH_GUARD() ((void)0)
#define XV_OBJECT_MATH_PRIVATE(c,kind,out,bytes,scratch,scratch_bytes) ((void)0)
#endif
