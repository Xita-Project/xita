#pragma once
#include "../xv_x86rt.h"
#include <limits.h>

/* Experimental 54132 loop batch. Caller is at that loop's entry in the
 * audited 54010 owner-side walk. Reads RAM only; no MMIO or concurrent writers.
 * Supply the containing lift's captured arena/page-table roots. No yield or
 * callback may occur inside a batch. The final iteration remains original.
 * No production hook or runtime enable switch is installed yet. Inlining keeps
 * rejected/short batches from paying a per-backedge function call. */

static inline __attribute__((always_inline)) unsigned xv_surface_scan_batch(xctx *c, uint8_t *arena, const uint32_t *pages)
{
    uint32_t cursor=c->r[3],sp=c->r[4],threshold=c->r[1];
    /* Keep wrap, unaligned/cross-page bound loads and imminent handoffs in
     * the original loop. Each batch leaves at least one budget unit. */
    if((cursor&3)||cursor>UINT32_MAX-4||sp>UINT32_MAX-0x14||
       ((sp+0x14)&4095)>4092||c->preempt<=8)return 0;
    uint32_t bound;
    memcpy(&bound,arena+pages[(sp+0x14)>>12]+((sp+0x14)&4095),4);
    uint32_t next=cursor+4;
    if(next>=bound)return 0;
    unsigned count=(bound-1-next)/4+1;
    unsigned page_count=(4096-(next&4095))/4;
    if(count>page_count)count=page_count;
    if(count>(unsigned)c->preempt-1)count=(unsigned)c->preempt-1;
    if(count>128)count=128;
    if(count<2)return 0;
    const uint8_t *values=arena+pages[next>>12]+(next&4095);
    unsigned matched=0;uint32_t value=0,last=0;
    for(;matched<count;matched++){
        memcpy(&value,values+4*matched,4);
        if((int32_t)value>=(int32_t)threshold)break;
        last=value;
    }
    if(matched<2)return 0;
    /* State at the original backedge, after its non-yielding budget decrement.
     * Lazy flag payload, including unrelated CF/OF storage, remains exact. */
    c->r[3]=cursor+4*matched;c->r[6]=bound;c->preempt-=(int32_t)matched;
    X_FLAGS(XK_SUB,last,threshold,last-threshold,32);
    return matched;
}
