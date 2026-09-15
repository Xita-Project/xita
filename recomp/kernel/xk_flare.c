/* Halo 3925 flare results: preserve old metadata while the next list is built.
 * No guest stack calls, stale results, GPU resources or parallel game access.
 * Generated guards complete results before brightness reset/read or query reuse. */
#include "xk.h"
#include "xk_flare.h"
#include "../../runtime/xv_visibility.h"
#include <stdlib.h>

extern uint32_t xd3d_r_visibility_result(uint32_t id, uint32_t *pixels);
extern int xd3d_r_visibility_wait(uint32_t id, uint32_t timeout_us,
                                uint32_t *age, uint32_t *render_us);
extern volatile uint32_t xv_cur_fn __attribute__((weak));

#define FLARE_COUNT 0x2E34E0u
#define FLARE_LIST 0x2C76D0u
#define FLARE_LIMIT 1024u
#define FLARE_OBJECT 0x27FFB0u
#define FLARE_OBJECT_END 0x2BFFB0u
#define FLARE_SURFACE 0x2BFFD2u
#define FLARE_SURFACE_END FLARE_LIST

typedef struct {
    uint32_t destination; int32_t area;
#ifdef XV_FLARE_QUERY_OVERLAP
    uint32_t generation;
#endif
} flare_record;
static flare_record records[FLARE_LIMIT]; /* query ID is the retained list index */
static unsigned pending;
static uint32_t draining;
static uint64_t queued_us;
static int override = -1;
static unsigned deferred, eager, declined, retained, peak;
static unsigned barriers[XV_FLARE_BARRIERS], waits;
static uint64_t gap_us, wait_us;

#ifdef XV_FLARE_QUERY_OVERLAP
extern uint32_t xd3d_r_visibility_generation(uint32_t id);
extern uint32_t xd3d_r_visibility_result_generation(uint32_t id,uint32_t serial,uint32_t *pixels);
extern int xd3d_r_visibility_wait_generation(uint32_t id,uint32_t serial,uint32_t timeout_us);
static int overlap_override=-1, pending_overlap;
static unsigned overlap_batches, overlap_queries;
void xv_flare_query_overlap_override(int value) { overlap_override=value<0?-1:!!value; }
static int overlap_enabled(void)
{
    static int configured=-1;
    if (configured<0) {
        /* This code exists only in the opt-in history build. Native-resolution
         * hardware comparisons expose a substantial first-query wait; retain
         * exact generations by default here, with an explicit off switch. */
        const char *e=getenv("XV_FLARE_QUERY_OVERLAP"); configured=!e || atoi(e)!=0;
    }
    return overlap_override<0 ? configured : overlap_override;
}
#endif

void xv_flare_defer_override(int enabled) { override=enabled<0?-1:!!enabled; }

static int enabled(void)
{
    static int configured=-1, compatible;
    if (configured<0) {
        const char *e=getenv("XV_FLARE_DEFER"); configured=!e || atoi(e)!=0;
        e=getenv("XV_VIS_STALE"); compatible=!e || atoi(e)==0;
        XK_LOG("deferred exact flare results: %s (default on; XV_FLARE_DEFER=0 disables)\n",
               configured && compatible ? "on" : "off");
    }
    return compatible && (override<0?configured:override);
}

/* Match the original 32-bit multiply/add wrap, signed divide and byte cast.
 * Zero coverage clears immediately; rising/falling brightness smooth differently. */
static uint8_t intensity(uint8_t old, int32_t area, uint32_t pixels)
{
    if (area<=0) return 0;
    uint32_t numerator=pixels*255u+((uint32_t)area>>1);
    int32_t ratio=(int32_t)numerator/area;
    uint8_t target=ratio>=255?255:(uint8_t)ratio;
    if (!target) return 0;
    if (target>old) return (uint8_t)((3u*old+target)/4u);
    if (target<old) return (uint8_t)((old+target)/2u);
    return old;
}

static uint32_t read_pixels(unsigned id,uint32_t *pixels)
{
#ifdef XV_FLARE_QUERY_OVERLAP
    if (pending_overlap)
        return xd3d_r_visibility_result_generation(id,records[id].generation,pixels);
#endif
    return xd3d_r_visibility_result(id,pixels);
}

static uint32_t exact_pixels(unsigned id)
{
    /* The lifted helper initializes its output to -1 and retains it on error. */
    uint32_t pixels=UINT32_MAX, result=read_pixels(id,&pixels);
    if (result!=XV_VISIBILITY_INCOMPLETE) return pixels;
    static int poll=-1, events, backoff;
    if (poll<0) {
        const char *e=getenv("XV_VISIBILITY_POLL_US"); poll=e?atoi(e):100;
        if (poll<0) poll=0;
        if (poll>1000) poll=1000;
        e=getenv("XV_VISIBILITY_EVENTS"); events=!e || atoi(e)!=0;
        e=getenv("XV_VISIBILITY_BACKOFF"); backoff=!e || atoi(e)!=0;
    }
    unsigned retry=0;
    uint64_t started=xk_os_monotonic_us();
    do {
        uint32_t age=UINT32_MAX, elapsed=0;
        int parked=0;
        if (events) {
#ifdef XV_FLARE_QUERY_OVERLAP
            if (pending_overlap)
                parked=xd3d_r_visibility_wait_generation(id,records[id].generation,100000);
            else
#endif
                parked=xd3d_r_visibility_wait(id,100000,&age,&elapsed);
        }
        if (!parked) {
            unsigned delay=(unsigned)poll;
            if (backoff) { delay<<=retry; if(delay>1000)delay=1000; if(retry<4)retry++; }
            if (delay) xk_sleep_us(delay); else xk_yield();
        }
        result=read_pixels(id,&pixels);
    } while (result==XV_VISIBILITY_INCOMPLETE);
    waits++; wait_us+=xk_os_monotonic_us()-started;
    return pixels;
}

void xv_flare_barrier(unsigned reason)
{
    /* Only one guest fiber executes at a time. A drain can park for the GPU;
     * a second fiber reaching a barrier must park until its writes finish.
     * The native result reader cannot re-enter guest code on its own fiber. */
    while (__atomic_load_n(&draining,__ATOMIC_ACQUIRE))
        xk_wait_u32(&draining,0,100000);
    if (!pending || reason==XV_FLARE_COLLECTION) return;
#ifdef XV_FLARE_QUERY_OVERLAP
    /* Retain the exact old serial across new Begin/End calls. Brightness,
     * identity, reset, next-list and Present barriers still drain all reads. */
    if (reason==XV_FLARE_QUERY && pending_overlap) { overlap_queries++; return; }
#endif
    __atomic_store_n(&draining,1,__ATOMIC_RELEASE);
    if (reason<XV_FLARE_BARRIERS) barriers[reason]++;
    gap_us+=xk_os_monotonic_us()-queued_us;
    uint32_t saved_fn=&xv_cur_fn?xv_cur_fn:0;
    if (&xv_cur_fn) xv_cur_fn=0x801810F0u;
    for (unsigned i=0;i<pending;i++) {
        uint32_t pixels=records[i].area>0?exact_pixels(i):0;
        uint8_t *out=g_img_base+records[i].destination;
        *out=intensity(*out,records[i].area,pixels);
    }
    pending=0;
#ifdef XV_FLARE_QUERY_OVERLAP
    pending_overlap=0;
#endif
    if (&xv_cur_fn) xv_cur_fn=saved_fn;
    __atomic_store_n(&draining,0,__ATOMIC_RELEASE);
    xk_os_scheduler_notify();
}

static int supported_caller(xctx *c)
{
    uint32_t sp=c->r[4];
    if (X_M32(sp)!=0x8022Au) return 0;
    uint32_t parent=X_M32(sp+4);
    if (parent==0x5D288u || parent==0xBC624u) return 1;
    /* With at least one view, every branch defines EDX before reading it.
     * Keep the zero-view path eager; EDX may escape the enclosing helper. */
    return parent==0x5DC0Cu && (int16_t)X_M16(c->r[5]+0xCu)>0;
}

int xv_flare_defer(xctx *c)
{
    xv_flare_barrier(XV_FLARE_NEXT);
    if (!enabled()) return 0;
    int32_t count=(int32_t)X_IMG32(FLARE_COUNT);
    if (!supported_caller(c) || count<0 || count>(int32_t)FLARE_LIMIT) { declined++; return 0; }
    if (!count) { eager++; return 0; }
#ifdef XV_FLARE_QUERY_OVERLAP
    int overlap=overlap_enabled();
#endif
    int incomplete=0;
    for (unsigned i=0;i<(unsigned)count;i++) {
        uint32_t row=FLARE_LIST+i*40u;
        uint16_t a=X_IMG16(row+0x1Eu);
        int32_t b=(int16_t)X_IMG16(row+0x20u);
        unsigned stage=X_IMG8(row+0x22u)&0x7Fu;
        uint32_t dest;
        if (a&0x8000u) {
            dest=FLARE_OBJECT+((((uint32_t)a&0x7FFFu)<<16)|(uint32_t)b)*4u+stage;
            if (dest<FLARE_OBJECT || dest>=FLARE_OBJECT_END) { declined++; return 0; }
        } else {
            dest=FLARE_SURFACE+(uint32_t)(int16_t)a*34u+(uint32_t)b*4u+stage;
            if (dest<FLARE_SURFACE || dest>=FLARE_SURFACE_END) { declined++; return 0; }
        }
        records[i]=(flare_record){dest,(int32_t)X_IMG32(row+0x24u)};
#ifdef XV_FLARE_QUERY_OVERLAP
        if (overlap && records[i].area>0) {
            records[i].generation=xd3d_r_visibility_generation(i);
            if (!records[i].generation) { declined++; return 0; }
        }
#endif
        if (records[i].area>0 && !incomplete) {
            uint32_t pixels=UINT32_MAX;
            incomplete=xd3d_r_visibility_result(i,&pixels)==XV_VISIBILITY_INCOMPLETE;
        }
    }
    if (!incomplete) { eager++; return 0; }
    queued_us=xk_os_monotonic_us();
    pending=(unsigned)count;
#ifdef XV_FLARE_QUERY_OVERLAP
    pending_overlap=overlap;
    overlap_batches+=(unsigned)overlap;
#endif
    retained+=pending; deferred++; if(pending>peak)peak=pending;
    X_IMG32(FLARE_COUNT)=0;
    /* All original volatile outputs are dead at the supported enclosing calls.
     * Callee-saved registers and the caller's live stack remain untouched. */
    c->r[4]+=4;
    return 1;
}

void xv_flare_report(unsigned frames)
{
    XK_LOG("[flare-defer] %u frames: %u deferred %u eager %u declined; %u records max %u; barriers next/query/brightness/identity/reset/present %u/%u/%u/%u/%u/%u; gap-us %llu waits %u wait-us %llu\n",
        frames,deferred,eager,declined,retained,peak,barriers[0],barriers[1],barriers[2],barriers[3],barriers[4],barriers[5],
        (unsigned long long)gap_us,waits,(unsigned long long)wait_us);
    deferred=eager=declined=retained=peak=waits=0;gap_us=wait_us=0;
#ifdef XV_FLARE_QUERY_OVERLAP
    XK_LOG("[flare-query-overlap] %u frames: %u retained-generation batches / %u query barriers postponed; exact results, Present still drains\n",
           frames,overlap_batches,overlap_queries);
    overlap_batches=overlap_queries=0;
#endif
    memset(barriers,0,sizeof barriers);
}
