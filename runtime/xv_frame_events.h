/* Notifications complement the acquire/release frame counters. Sticky bits
 * cover completion before WaitEventFlag; counters remain the source of truth. */
#pragma once
#include <stdint.h>
#include <psp2/kernel/threadmgr.h>

enum { XV_FRAME_REQUESTED = 1u, XV_FRAME_COMPLETED = 2u };
typedef struct { SceUID id; } xv_frame_events;

static inline void xv_frame_events_init(xv_frame_events *events, int enabled)
{
    events->id=enabled ? sceKernelCreateEventFlag("xv_frames",SCE_EVENT_WAITMULTIPLE,0,NULL) : -1;
}
static inline void xv_frame_events_signal(const xv_frame_events *events, unsigned bits)
{
    if (events->id>=0) sceKernelSetEventFlag(events->id,bits);
}
static inline void xv_frame_events_wait(const xv_frame_events *events, unsigned bits, unsigned fallback_us)
{
    if (events->id<0) { sceKernelDelayThread(fallback_us); return; }
    SceUInt timeout=10000;
    unsigned matched=0;
    /* Clear only the waited-for pattern, never the other thread's wake bit.
     * A bounded wait also lets shutdown or failed notifications be observed. */
    int rc=sceKernelWaitEventFlag(events->id,bits,SCE_EVENT_WAITOR|SCE_EVENT_WAITCLEAR_PAT,&matched,&timeout);
    if (rc<0 && timeout) sceKernelDelayThread(fallback_us);
}
static inline void xv_frame_events_close(xv_frame_events *events)
{
    if (events->id>=0) sceKernelDeleteEventFlag(events->id);
    events->id=-1;
}
