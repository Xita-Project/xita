#ifndef XV_CPU_H
#define XV_CPU_H

#include <stdint.h>

/* Pump-thread poll, at most one kernel counter read per second. now_us is
 * sceKernelGetProcessTimeWide(), not a game clock or CPU cycle counter. */
void xv_cpu_poll(uint64_t now_us);

/* One atomic snapshot: bytes 0..2 hold core 0..2 busy percentages (0..100).
 * 255 means no valid interval yet, disabled, or counters unavailable. */
#define XV_CPU_UNKNOWN 255u
uint32_t xv_cpu_usage(void);

/* XV_THREADS=1: report the calling thread's effective affinity and identity.
 * Startup calls are unthrottled; guest polling is limited to once a second and
 * must only be called by the serial guest execution path. No affinity changes. */
void xv_cpu_log_thread(const char *role);
void xv_cpu_guest_poll(void);

#endif
