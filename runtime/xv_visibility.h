/* Guest issues queries; the render thread publishes after GPU completion. */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define XV_VISIBILITY_IDS 512u
#define XV_VISIBILITY_PER_FRAME 512u
#define XV_VISIBILITY_INCOMPLETE 0x88760828u
#define XV_VISIBILITY_OUT_OF_MEMORY 0x8007000eu
#define XV_VISIBILITY_INVALID_ARGUMENT 0x80070057u

#ifdef XV_FLARE_QUERY_OVERLAP
/* One render owner publishes. Exact readers name a generation; the history
 * never substitutes an older result. Halo drains retained reads before the
 * next Present, so a later frame cannot overwrite their completed generation. */
#define XV_VISIBILITY_HISTORY 4u
typedef struct { uint32_t sequence, serial, pixels; } xv_visibility_sample;
#endif

typedef struct {
    uint32_t id, used;
    uint32_t issued, completed, pixels;
    uint32_t submitted, submitted_us, completed_us;
#ifdef XV_FLARE_QUERY_OVERLAP
    xv_visibility_sample history[XV_VISIBILITY_HISTORY];
#endif
} xv_visibility_result;

static inline xv_visibility_result *xv_visibility_find(xv_visibility_result *results,uint32_t id)
{
    if (id<XV_VISIBILITY_IDS && results[id].used && results[id].id==id) return &results[id];
    for (unsigned i=0;i<XV_VISIBILITY_IDS;i++)
        if (results[i].used && results[i].id==id) return &results[i];
    return NULL;
}

static inline void xv_visibility_submit(xv_visibility_result *results,unsigned slot,uint32_t serial,uint32_t us)
{
    results[slot].submitted_us=us;
    __atomic_store_n(&results[slot].submitted,serial,__ATOMIC_RELEASE);
}

static inline void xv_visibility_publish_timed(xv_visibility_result *results,
    unsigned slot,uint32_t serial,uint32_t pixels,uint32_t us)
{
    if (slot>=XV_VISIBILITY_IDS || !serial) return;
#ifdef XV_FLARE_QUERY_OVERLAP
    xv_visibility_sample *h=&results[slot].history[serial % XV_VISIBILITY_HISTORY];
    uint32_t sequence=__atomic_load_n(&h->sequence,__ATOMIC_SEQ_CST);
    /* Atomic fields and a sequence check also protect a reader when its
     * generation has aged out. It must never pair old serial/new pixels. */
    __atomic_store_n(&h->sequence,sequence+1u,__ATOMIC_SEQ_CST);
    __atomic_store_n(&h->pixels,pixels,__ATOMIC_SEQ_CST);
    __atomic_store_n(&h->serial,serial,__ATOMIC_SEQ_CST);
    __atomic_store_n(&h->sequence,sequence+2u,__ATOMIC_SEQ_CST);
#endif
    __atomic_store_n(&results[slot].pixels,pixels,__ATOMIC_RELAXED);
    results[slot].completed_us=us;
    __atomic_store_n(&results[slot].completed,serial,__ATOMIC_RELEASE);
}

static inline uint32_t xv_visibility_issue(xv_visibility_result *results,
    uint32_t id, uint16_t *slot, uint32_t *serial)
{
    unsigned i=id;
    /* Halo reuses consecutive small IDs. Their original first-empty slots
     * usually match the ID; verify that match because arbitrary IDs and issue
     * order are also valid. Keep the existing allocation policy on a miss. */
    if (i >= XV_VISIBILITY_IDS || !results[i].used || results[i].id != id) {
        unsigned empty = XV_VISIBILITY_IDS;
        for (i=0;i<XV_VISIBILITY_IDS;i++) {
            if (results[i].used && results[i].id == id) break;
            if (!results[i].used && empty == XV_VISIBILITY_IDS) empty=i;
        }
        if (i == XV_VISIBILITY_IDS) {
            if (empty == XV_VISIBILITY_IDS) return XV_VISIBILITY_OUT_OF_MEMORY;
            i=empty; results[i].id=id; results[i].used=1;
        }
    }
    uint32_t next=results[i].issued+1u;
    if (!next) next=1;
    __atomic_store_n(&results[i].issued,next,__ATOMIC_RELEASE);
    *slot=(uint16_t)i; *serial=next;
    return 0;
}

static inline void xv_visibility_publish(xv_visibility_result *results,
    unsigned slot, uint32_t serial, uint32_t pixels)
{
    xv_visibility_publish_timed(results,slot,serial,pixels,0);
}

static inline uint32_t xv_visibility_read(const xv_visibility_result *results,
    uint32_t id, uint32_t *pixels)
{
    unsigned i=id;
    if (i >= XV_VISIBILITY_IDS || !results[i].used || results[i].id != id) {
        for (i=0;i<XV_VISIBILITY_IDS;i++)
            if (results[i].used && results[i].id == id) break;
    }
    if (i == XV_VISIBILITY_IDS) return XV_VISIBILITY_INVALID_ARGUMENT;
    uint32_t issued=__atomic_load_n(&results[i].issued,__ATOMIC_ACQUIRE);
    if (!issued || __atomic_load_n(&results[i].completed,__ATOMIC_ACQUIRE) != issued)
        return XV_VISIBILITY_INCOMPLETE;
    *pixels=__atomic_load_n(&results[i].pixels,__ATOMIC_RELAXED);
    return 0;
}

#ifdef XV_FLARE_QUERY_OVERLAP
static inline uint32_t xv_visibility_generation(xv_visibility_result *results,uint32_t id)
{
    xv_visibility_result *r=xv_visibility_find(results,id);
    return r ? __atomic_load_n(&r->issued,__ATOMIC_ACQUIRE) : 0;
}

static inline uint32_t xv_visibility_read_generation(xv_visibility_result *results,
    uint32_t id,uint32_t serial,uint32_t *pixels)
{
    xv_visibility_result *r=xv_visibility_find(results,id);
    if (!r || !serial) return XV_VISIBILITY_INVALID_ARGUMENT;
    const xv_visibility_sample *h=&r->history[serial % XV_VISIBILITY_HISTORY];
    uint32_t first=__atomic_load_n(&h->sequence,__ATOMIC_SEQ_CST);
    if (first&1u) return XV_VISIBILITY_INCOMPLETE;
    uint32_t found=__atomic_load_n(&h->serial,__ATOMIC_SEQ_CST);
    uint32_t value=__atomic_load_n(&h->pixels,__ATOMIC_SEQ_CST);
    if (first!=__atomic_load_n(&h->sequence,__ATOMIC_SEQ_CST) || found!=serial)
        return XV_VISIBILITY_INCOMPLETE;
    *pixels=value;
    return 0;
}
#endif

/* Experimental approximate read, disabled by default in the HLE. A numerical
 * query ID alone establishes neither object identity nor a bound on frame age.
 * This can return a result more than one frame old and must not satisfy an
 * exact-completion request. Returns INCOMPLETE if the ID has never completed. */
static inline uint32_t xv_visibility_read_stale(const xv_visibility_result *results,
    uint32_t id, uint32_t *pixels, uint32_t *behind)
{
    unsigned i=id;
    if (i >= XV_VISIBILITY_IDS || !results[i].used || results[i].id != id) {
        for (i=0;i<XV_VISIBILITY_IDS;i++)
            if (results[i].used && results[i].id == id) break;
    }
    if (i == XV_VISIBILITY_IDS) return XV_VISIBILITY_INVALID_ARGUMENT;
    uint32_t issued=__atomic_load_n(&results[i].issued,__ATOMIC_ACQUIRE);
    uint32_t completed=__atomic_load_n(&results[i].completed,__ATOMIC_ACQUIRE);
    if (!completed) return XV_VISIBILITY_INCOMPLETE;
    *pixels=__atomic_load_n(&results[i].pixels,__ATOMIC_RELAXED);
    if (behind) *behind=issued-completed;
    return 0;
}

/* Halo divides by a rectangle's guest-screen area. Preserve that ratio when
 * the Vita draws the same clip-space rectangle at a different resolution. */
static inline uint32_t xv_visibility_scale(uint64_t pixels, uint32_t guest_area,
    uint32_t render_area)
{
    if (guest_area && render_area) pixels=(pixels*guest_area+render_area/2u)/render_area;
    return pixels>UINT32_MAX ? UINT32_MAX : (uint32_t)pixels;
}
