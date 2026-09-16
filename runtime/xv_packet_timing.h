#ifndef XV_PACKET_TIMING_H
#define XV_PACKET_TIMING_H
#include <stdint.h>
#include <string.h>

#ifndef XV_GPU_PACKET_TIMING
#define XV_GPU_PACKET_TIMING 0
#endif
#if XV_GPU_PACKET_TIMING
/* Diagnostic state belongs exclusively to the render pump. GPU/display
 * callbacks never access it. Fold before publishing packet retirement. */
typedef struct {
    uint64_t begin, end, last_before, last_after, negative_before, ready_after;
    uint64_t polls, max_gap;
    uint32_t ticket;
    unsigned submitted, ready, negative, failed, invalid, folded;
} xv_packet_timing;
enum {
    XV_PACKET_SUBMIT, XV_PACKET_LOWER, XV_PACKET_UPPER,
    XV_PACKET_TAIL_LOWER, XV_PACKET_TAIL_UPPER, XV_PACKET_BRACKET,
    XV_PACKET_POLLS, XV_PACKET_GAP, XV_PACKET_METRICS
};
typedef struct {
    uint64_t sum[XV_PACKET_METRICS], max[XV_PACKET_METRICS];
    unsigned retired, valid, failed, invalid, no_negative;
    uint32_t first_ticket, last_ticket;
} xv_packet_timing_totals;

/* Unsigned subtraction crosses uint64 wrap. Reject backwards/discontinuous
 * clocks instead of producing giant durations; valid spans must be <2^63 us. */
static inline int xv_packet_delta(uint64_t later, uint64_t earlier, uint64_t *out)
{
    *out = later - earlier;
    return *out <= INT64_MAX;
}
static inline void xv_packet_timing_begin(xv_packet_timing *p, uint32_t ticket,
                                         uint64_t begin, int stale)
{
    *p = (xv_packet_timing){.begin=begin, .last_before=begin, .last_after=begin,
                          .ticket=ticket, .invalid=!!stale};
}
static inline void xv_packet_timing_end(xv_packet_timing *p, uint64_t end, int failed)
{
    uint64_t duration;
    if (p->submitted || !xv_packet_delta(end,p->begin,&duration)) p->invalid=1;
    p->end=end; p->submitted=1; p->failed=!!failed;
}
/* before <= notification load <= after. A failed load proves completion was
 * later than before, NOT later than after: the GPU can write during the read's
 * trailing clock call. First-ready without a failed load has only begin as
 * its lower bound, even if the CPU submission call has already returned. */
static inline void xv_packet_timing_observe(xv_packet_timing *p,
                                           uint64_t before, uint64_t after, int ready)
{
    uint64_t gap, unused;
    if (p->ready) { if (!ready) p->invalid=1; return; }
    if (p->invalid || p->failed) return;
    if (!p->submitted || p->folded ||
        !xv_packet_delta(before,p->end,&unused) ||
        !xv_packet_delta(before,p->last_after,&unused) ||
        !xv_packet_delta(after,before,&unused) ||
        !xv_packet_delta(after,p->last_before,&gap) ||
        !xv_packet_delta(after,p->begin,&unused) || p->polls==UINT64_MAX) {
        p->invalid=1; return;
    }
    p->polls++;
    if (gap>p->max_gap) p->max_gap=gap;
    p->last_before=before; p->last_after=after;
    if (ready) { p->ready=1; p->ready_after=after; }
    else { p->negative=1; p->negative_before=before; }
}
static inline void xv_packet_timing_fold(xv_packet_timing_totals *a, xv_packet_timing *p)
{
    if (!a->retired) a->first_ticket=p->ticket;
    a->last_ticket=p->ticket; a->retired++;
    if (p->folded) { a->invalid++; return; }
    p->folded=1;
    if (p->failed) { a->failed++; return; }
    uint64_t v[XV_PACKET_METRICS], lower=p->negative?p->negative_before:p->begin;
    if (p->invalid || !p->submitted || !p->ready ||
        !xv_packet_delta(p->end,p->begin,&v[XV_PACKET_SUBMIT]) ||
        !xv_packet_delta(lower,p->begin,&v[XV_PACKET_LOWER]) ||
        !xv_packet_delta(p->ready_after,p->begin,&v[XV_PACKET_UPPER]) ||
        !xv_packet_delta(p->ready_after,lower,&v[XV_PACKET_BRACKET])) {
        a->invalid++; return;
    }
    /* Completion can occur DURING CPU submission. These clamped tails bound
     * remaining completion latency after the call returns, not GPU duration. */
    v[XV_PACKET_TAIL_LOWER]=v[XV_PACKET_LOWER]>v[XV_PACKET_SUBMIT]?
        v[XV_PACKET_LOWER]-v[XV_PACKET_SUBMIT]:0;
    v[XV_PACKET_TAIL_UPPER]=v[XV_PACKET_UPPER]>v[XV_PACKET_SUBMIT]?
        v[XV_PACKET_UPPER]-v[XV_PACKET_SUBMIT]:0;
    v[XV_PACKET_POLLS]=p->polls; v[XV_PACKET_GAP]=p->max_gap;
    for (unsigned i=0;i<XV_PACKET_METRICS;i++)
        if (v[i]>UINT64_MAX-a->sum[i]) { a->invalid++; return; }
    for (unsigned i=0;i<XV_PACKET_METRICS;i++) {
        a->sum[i]+=v[i]; if(v[i]>a->max[i])a->max[i]=v[i];
    }
    a->valid++; a->no_negative+=!p->negative;
}
/* One report per existing retirement window, never on the GPU callback or
 * per-poll path. Totals/denominator include only valid successful packets. */
static inline void xv_packet_timing_report(xv_packet_timing_totals *a)
{
    extern void xv_logf(const char *, ...);
    xv_logf("[gpu-packet] tickets %u..%u retired %u valid %u failed %u invalid %u no-negative %u; sum-us submit %llu completion-lo/hi %llu/%llu after-submit-lo/hi %llu/%llu bracket %llu; polls total/max %llu/%llu; max-us submit %llu bracket %llu observation-gap %llu; CPU-observed bounds, not GPU service time\n",
        a->first_ticket,a->last_ticket,a->retired,a->valid,a->failed,a->invalid,a->no_negative,
        (unsigned long long)a->sum[XV_PACKET_SUBMIT],
        (unsigned long long)a->sum[XV_PACKET_LOWER],(unsigned long long)a->sum[XV_PACKET_UPPER],
        (unsigned long long)a->sum[XV_PACKET_TAIL_LOWER],(unsigned long long)a->sum[XV_PACKET_TAIL_UPPER],
        (unsigned long long)a->sum[XV_PACKET_BRACKET],
        (unsigned long long)a->sum[XV_PACKET_POLLS],(unsigned long long)a->max[XV_PACKET_POLLS],
        (unsigned long long)a->max[XV_PACKET_SUBMIT],(unsigned long long)a->max[XV_PACKET_BRACKET],
        (unsigned long long)a->max[XV_PACKET_GAP]);
    memset(a,0,sizeof *a);
}
#endif
#endif
