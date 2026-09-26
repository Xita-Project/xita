#ifndef XV_DISPLAY_TIMING_MAILBOX_H
#define XV_DISPLAY_TIMING_MAILBOX_H
#include <stdint.h>
/* One callback producer, one pump consumer. No waiting and no 64-bit atomics.
 * Producer retains its aggregate when full; consumer releases after copying. */
typedef struct { uint64_t setup_us,vblank_us,max_us; unsigned count; } xv_display_timing_sample;
typedef struct { uint32_t ready; xv_display_timing_sample sample; } xv_display_timing_mailbox;
_Static_assert(__atomic_always_lock_free(4,0), "32-bit atomics required");
static inline int xv_display_timing_push(xv_display_timing_mailbox *m, xv_display_timing_sample s)
{
    if (__atomic_load_n(&m->ready,__ATOMIC_ACQUIRE)) return 0;
    m->sample=s; __atomic_store_n(&m->ready,1,__ATOMIC_RELEASE); return 1;
}
static inline int xv_display_timing_pop(xv_display_timing_mailbox *m, xv_display_timing_sample *s)
{
    if (!__atomic_load_n(&m->ready,__ATOMIC_ACQUIRE)) return 0;
    *s=m->sample; __atomic_store_n(&m->ready,0,__ATOMIC_RELEASE); return 1;
}
#endif
