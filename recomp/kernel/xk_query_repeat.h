#ifndef XK_QUERY_REPEAT_H
#define XK_QUERY_REPEAT_H
#include <stdint.h>
#include <stddef.h>

/* Diagnostic only. Caller validates memory/owner/guard and supplies values.
 * Matches do not prove geometry immutability or safe query replay. */
#define XV_QUERY_REPEAT_ENTRIES 64u
#define XV_QUERY_REPEAT_FILTER_BYTES 32u
#define XV_QUERY_REPEAT_FP_CONTROL_MASK 0x07f79f00u

typedef struct {
    uintptr_t arena,pages,image;
    uint32_t bsp,filter_bits,filter,point;
    uint32_t center[3],radius,fcw,fp_control;
    uint32_t zero,selector[6],geometry[24];
} XvQueryRepeatKey;
typedef struct {
    uint64_t within_epoch,prior_epoch_only,misses;
} XvQueryRepeatTier;
typedef struct {
    uint64_t calls,valid,invalid,filter_valid,filter_unavailable;
    uint64_t evictions,epochs,invalidations;
    XvQueryRepeatTier input,filter;
} XvQueryRepeatCounts;
typedef struct {
    XvQueryRepeatKey key;
    uint64_t epoch;
    uint32_t hash;
    unsigned filter_valid;
    unsigned char filter[XV_QUERY_REPEAT_FILTER_BYTES];
} XvQueryRepeatEntry;
typedef struct {
    XvQueryRepeatEntry entries[XV_QUERY_REPEAT_ENTRIES];
    XvQueryRepeatCounts counts;
    uint64_t epoch;
    unsigned used,next;
} XvQueryRepeat;

/* Caller owns synchronization: one serialized writer; reports/epoch changes
 * only after joining writers. Epoch means an object pass, not a render frame.
 * init requires unused/joined state; all other calls require prior init. */
void xv_query_repeat_init(XvQueryRepeat *state);
void xv_query_repeat_advance_epoch(XvQueryRepeat *state);
/* Root/owner lifetime invalidation clears history, preserving report counts. */
void xv_query_repeat_invalidate(XvQueryRepeat *state);
/* NULL key records invalid input. NULL filter records input-tier only.
 * Both non-NULL inputs are copied; no caller pointers are retained. The ring
 * retains the last64 valid observations, including repeated inputs. */
void xv_query_repeat_observe(XvQueryRepeat *state,const XvQueryRepeatKey *key,
    const unsigned char filter[XV_QUERY_REPEAT_FILTER_BYTES]);
/* Joined report reset preserves observation history and current epoch. */
void xv_query_repeat_take(XvQueryRepeat *state,XvQueryRepeatCounts *out);
uint32_t xv_query_repeat_hash(const XvQueryRepeatKey *key);
#endif
