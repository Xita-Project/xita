/* xk_os.h - the small OS surface the kernel translator needs (host: POSIX+ucontext, Vita: sceIo+xk_sched). */
#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct xk_file  xk_file;
typedef struct xk_dir   xk_dir;
typedef struct xk_fiber xk_fiber;

void      xk_os_log(const char *fmt, ...);

/* files */
xk_file  *xk_os_open(const char *path, int write, int create, int truncate, int *err_is_missing);
void      xk_os_close(xk_file *f);
int64_t   xk_os_read(xk_file *f, uint64_t pos, void *buf, uint32_t n);
int64_t   xk_os_write(xk_file *f, uint64_t pos, const void *buf, uint32_t n);
int64_t   xk_os_size(xk_file *f);
int       xk_os_truncate(xk_file *f, uint64_t size);
int       xk_os_flush(xk_file *f);
int       xk_os_stat(const char *path, int *is_dir, uint64_t *size, uint64_t *mtime_100ns);   /* 0 ok, -1 missing */
int       xk_os_unlink(const char *path);
int       xk_os_mkdir(const char *path);
int       xk_os_rmdir(const char *path);
int       xk_os_rename(const char *from, const char *to);
xk_dir   *xk_os_opendir(const char *path);
int       xk_os_readdir(xk_dir *d, char *name, unsigned cap, int *is_dir, uint64_t *size);   /* 1 entry, 0 end */
void      xk_os_closedir(xk_dir *d);
int       xk_os_freespace(const char *path, uint64_t *free_bytes, uint64_t *total_bytes);

/* time */
uint64_t  xk_os_time_100ns(void);            /* wall clock since 1601-01-01 */
uint64_t  xk_os_monotonic_us(void);
/* The Vita thread id that host-side owner checks compare against: the caller's id, or the owner's id
 * while the scene helper (xk_scene_thread.c) runs the owner's scene body on its behalf. */
int       xv_owner_thread_id(void);
void      xk_os_sleep_us(uint64_t us);

/* Sticky notification interrupts scheduler idle waits, not guest timers. */
int       xk_os_scheduler_prepare(void);
void      xk_os_scheduler_notify(void); /* may be called by the render thread */
void      xk_os_scheduler_wait(uint64_t us);

/* fibers: cooperative, one host stack each; guest code runs on guest stacks via esp */
xk_fiber *xk_os_fiber_create(void (*entry)(void *), void *arg, size_t host_stack);
void      xk_os_fiber_switch(xk_fiber *to);  /* from current */
xk_fiber *xk_os_fiber_current(void);
xk_fiber *xk_os_fiber_main(void);            /* the scheduler's own context */
void      xk_os_fiber_destroy(xk_fiber *f);  /* never the current one */

/* input (XInput HLE) */
typedef struct { uint16_t buttons; uint8_t analog[8]; int16_t lx, ly, rx, ry; int connected; uint16_t p2_buttons; uint8_t p2_analog[8]; int force_start; } xk_os_pad;   /* force_start: L+R+Triangle chord / "force" script token (XV_FORCE_START) */   /* p2_*: virtual second gamepad (XV_PAD2) */
void      xk_os_pad_poll(xk_os_pad *p);
