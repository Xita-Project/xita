/* Real uploader, independent expected bytes and deterministic elapsed clock. */
#include <stdint.h>
static uint64_t profile_clock;
#define XV_VERTEX_UPLOAD_BYTES (2u * 1024u * 1024u)
#define main original_upload_checks
#include "../../recomp/host/vertex_upload_test.c"
#undef main
#ifndef NO_PROFILE_CLOCK
uint64_t xk_os_monotonic_us(void) { return profile_clock++; }
#endif

int main(void)
{
    const unsigned sizes[] = {1,4096,4097,16384,16385,65536,65537,262144,262145};
    unsigned char *source = malloc(262145); assert(source);
    xv_vertex_upload_override(0); xv_vertex_worker_override(0); xv_vertex_copy_override(0);
    for (unsigned i=0;i<sizeof sizes/sizeof *sizes;i++) {
        unsigned n=sizes[i]; memset(source,0x3b,n);
        const unsigned char *old=xv_vertex_upload(0,source,n);
        assert(old && !memcmp(old,source,n));
        assert(xv_vertex_upload(0,source,n)==old);
        source[0]^=0xff;
        const unsigned char *fresh=xv_vertex_upload(0,source,n);
        assert(fresh && fresh!=old && !memcmp(fresh,source,n));
        assert(old[0]==0x3b);
        for(unsigned j=1;j<n;j++)assert(old[j]==0x3b);
        xv_vertex_upload_shutdown();assert(!live);next_id=1;
    }
    free(source);
#ifdef NO_PROFILE_CLOCK
    assert(!profile_clock);
#endif
#if defined(XV_VERTEX_PROFILE) && XV_VERTEX_PROFILE
    unsigned expected_calls[]={2,2,2,2,1};
    uint64_t expected_bytes[]={4097,20481,81921,327681,262145};
    for(unsigned kind=0;kind<XV_VERTEX_WORK_COUNT;kind++)
        for(unsigned bin=0;bin<XV_VERTEX_WORK_BINS;bin++) {
            uint64_t factor=kind==XV_VERTEX_DISPATCH?0:kind==XV_VERTEX_SNAPSHOT?2:1;
#ifdef NO_PROFILE_CLOCK
            factor=0;
#endif
            assert(xv_vertex_work_cost[kind][bin].calls==factor*expected_calls[bin]);
            assert(xv_vertex_work_cost[kind][bin].bytes==factor*expected_bytes[bin]);
            assert(xv_vertex_work_cost[kind][bin].us==factor*expected_calls[bin]);
        }
    uint64_t kept=xv_vertex_work_cost[XV_VERTEX_SNAPSHOT][0].calls;
    xv_vertex_work_report(0);assert(xv_vertex_work_cost[XV_VERTEX_SNAPSHOT][0].calls==kept);
    xv_vertex_work_report(9);
    for(unsigned kind=0;kind<XV_VERTEX_WORK_COUNT;kind++)
        for(unsigned bin=0;bin<XV_VERTEX_WORK_BINS;bin++)
            assert(!xv_vertex_work_cost[kind][bin].calls && !xv_vertex_work_cost[kind][bin].bytes && !xv_vertex_work_cost[kind][bin].us);
#else
    assert(!profile_clock); /* No instrumentation calls in ordinary builds. */
#endif
    puts("PASS: real upload/compare/snapshot boundaries, retained bytes, diagnostic accounting/reset, compiled-out or missing timer path");
    return 0;
}
