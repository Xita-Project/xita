#ifndef XV_FRAME_PACER_H
#define XV_FRAME_PACER_H
#include <stdint.h>

/* Optional presentation cap. Late frames start a fresh interval; never issue
 * catch-up bursts or alter the guest's real-time 60 Hz clock. */
static inline uint32_t xv_frame_pacer_wait(uint64_t now, uint64_t *next, uint32_t period)
{
    if (!period) { *next = 0; return 0; }
    uint64_t due = *next > now ? *next : now;
    *next = due + period;
    return (uint32_t)(due - now);
}
#endif
