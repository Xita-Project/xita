/* Exact production upload/worker paths. Record actual memcpy destinations,
 * rather than accepting the uploader's avoided-byte counters as an oracle. */
#include <string.h>
#include <pthread.h>
static void *record_copy(void *,const void *,size_t);
#define memcpy record_copy
#define XV_UPLOAD_WORKER_TEST_NO_MAIN
#include "upload_worker_test.c"
#undef memcpy
static struct {void *dst;size_t bytes;} writes[512];
static unsigned nwrites;
static pthread_mutex_t writes_lock=PTHREAD_MUTEX_INITIALIZER;
static void *record_copy(void *dst,const void *src,size_t bytes)
{
    pthread_mutex_lock(&writes_lock);
    assert(nwrites<sizeof writes/sizeof *writes);
    writes[nwrites].dst=dst;writes[nwrites++].bytes=bytes;
    pthread_mutex_unlock(&writes_lock);
    return memcpy(dst,src,bytes);
}
static void clear_trace(void)
{ pthread_mutex_lock(&writes_lock);nwrites=0;pthread_mutex_unlock(&writes_lock); }
/* Every test joins before inspection; use exact GPU writes, including caller
 * fallbacks, but exclude the separately recorded guest-to-mirror snapshots. */
static void expect_write(unsigned slot,unsigned count,unsigned offset,unsigned bytes)
{
    unsigned found=0;uintptr_t base=(uintptr_t)pools[slot].gpu;
    pthread_mutex_lock(&writes_lock);
    for(unsigned i=0;i<nwrites;i++) {
        uintptr_t p=(uintptr_t)writes[i].dst;
        if(p>=base && p<base+XV_VERTEX_UPLOAD_BYTES) {
            assert(found++<count && p-base==offset && writes[i].bytes==bytes);
        }
    }
    assert(found==count);pthread_mutex_unlock(&writes_lock);
}
static unsigned char source[3][16][8192], expected[3][131072];
static void seed(unsigned slot,unsigned bytes)
{
    for(unsigned i=0;i<bytes;i++)expected[slot][i]=(unsigned char)(i*13u+(i/8192u)*17u+slot*43u);
    memcpy(source[slot],expected[slot],bytes);
}
static void prime(unsigned slot,unsigned bytes)
{
    xv_vertex_worker_override(0);xv_vertex_upload_override(0);
    assert(xv_vertex_upload(slot,expected[slot],bytes));
    xv_vertex_upload_reset(slot);
    xv_vertex_worker_override(1);xv_vertex_upload_override(1);
}
static void exact_slot(unsigned slot,unsigned bytes)
{
    assert(!memcmp(pools[slot].gpu,expected[slot],bytes));
    assert(!memcmp(pools[slot].gpu,pools[slot].cpu,pools[slot].valid_bytes));
}
static void envelope(unsigned mask,int residency,int fail)
{
    seed(0,65536);prime(0,65536);clear_trace();
    xv_vertex_upload_override(residency);
    if(fail<=3)init_failure=fail;
    else if(fail==4)fail_signal=1;
    unsigned lo=8,hi=0,n=0;
    for(unsigned i=0;i<8;i++)if(mask&(1u<<i)) {
        source[0][i][2]^=0x5a;expected[0][i*8192+2]^=0x5a;
        if(lo==8)lo=i;
        hi=i+1;n++;
    }
    unsigned first=residency?lo*8192:0;
    unsigned bytes=residency?(hi?hi*8192-first:0):65536;
    uint64_t old_queued=gpu_queued_bytes,old_caller=gpu_caller_bytes;
    uint64_t old_clean=gpu_clean_bytes,old_gap=gpu_overcopy_bytes,old_avoided=resident_bytes;
    uint32_t before=submitted;
    for(unsigned i=0;i<8;i++) {
        const void *p=xv_vertex_upload(0,source[0][i],8192);assert(p==pools[0].gpu+i*8192);
        /* Subsequent guest mutations must not change either retained snapshot. */
        memset(source[0][i],0xe1,8192);
    }
    xv_vertex_upload_seal(0);xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
    expect_write(0,!!bytes,first,bytes);exact_slot(0,65536);
    assert(submitted-before==(unsigned)(bytes && !fail));
    assert(gpu_queued_bytes-old_queued==(fail?0:bytes));
    assert(gpu_caller_bytes-old_caller==(fail?bytes:0));
    assert(gpu_clean_bytes-old_clean==65536-bytes);
    assert(gpu_overcopy_bytes-old_gap==(residency?bytes-n*8192:0));
    assert(resident_bytes-old_avoided==(residency?(8-n)*8192:0));
    if(!bytes)assert(thread<0 && !pools[0].has_ticket);
    reset_worker();
}
static void padding_and_stale_prefix(void)
{
    seed(0,65536);clear_trace();xv_vertex_worker_override(1);xv_vertex_upload_override(1);
    assert(xv_vertex_upload(0,source[0],4097));xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
    expect_write(0,1,0,4112);
    for(unsigned i=4097;i<4112;i++)assert(!pools[0].gpu[i]);
    xv_vertex_upload_reset(0);clear_trace();
    assert(xv_vertex_upload(0,source[0],4097));
    unsigned char zeros[15]={0};assert(xv_vertex_upload(0,zeros,sizeof zeros));
    xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
    expect_write(0,1,4112,16); /* New alignment bytes are actual dirty writes. */
    for(unsigned i=4097;i<4128;i++)assert(!pools[0].gpu[i]);
    assert(!memcmp(pools[0].cpu,pools[0].gpu,pools[0].valid_bytes));reset_worker();

    seed(0,65536);prime(0,65536);clear_trace();
    source[0][0][0]^=31;expected[0][0]^=31;
    assert(xv_vertex_upload(0,source[0],64));xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
    expect_write(0,1,0,64);exact_slot(0,65536);
    assert(pools[0].valid_bytes==65536);xv_vertex_upload_reset(0);clear_trace();
    assert(xv_vertex_upload(0,expected[0],65536));xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
    expect_write(0,0,0,0);exact_slot(0,65536);reset_worker();
}
static void dirty_changes_after_resident_hit(void)
{
    seed(0,8192);prime(0,8192);clear_trace();
    const unsigned char *old=xv_vertex_upload(0,source[0],8192);assert(old);
    source[0][0][0]^=19;
    const unsigned char *new=xv_vertex_upload(0,source[0],8192);assert(new==old+8192);
    xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);expect_write(0,1,8192,8192);
    assert(!memcmp(old,expected[0],8192) && !memcmp(new,source[0],8192));reset_worker();
}
static int join_entered,join_done;
static unsigned join_slot,join_reset;
static void *join_copy(void *unused)
{
    (void)unused;__atomic_store_n(&join_entered,1,__ATOMIC_RELEASE);
    if(join_reset)xv_vertex_upload_reset(join_slot);else xv_vertex_upload_wait(join_slot);
    __atomic_store_n(&join_done,1,__ATOMIC_RELEASE);return NULL;
}
static void clean_tail_retains_ticket(unsigned reset)
{
    /* Prime synchronously so the first worker starts from our wrap witness. */
    for(unsigned s=0;s<3;s++){seed(s,131072);prime(s,131072);}
    submitted=completed=UINT32_MAX-1;assert(thread<0);
    __atomic_store_n(&pause_worker,1,__ATOMIC_RELEASE);clear_trace();
    for(unsigned s=0;s<3;s++) {
        source[s][0][0]^=7;expected[s][0]^=7;
        assert(xv_vertex_upload(s,source[s],65536));
        uint32_t ticket=pools[s].ticket;
        assert(ticket==(uint32_t)(UINT32_MAX+s));
        assert(xv_vertex_upload(s,&source[s][8],65536));
        xv_vertex_upload_seal(s);xv_vertex_upload_seal(s);
        assert(pools[s].has_ticket && pools[s].ticket==ticket && pools[s].dispatched==131072);
        memset(source[s],0xee,sizeof source[s]);
    }
    assert(submitted==1);join_slot=1;join_reset=reset;join_entered=join_done=0;
    pthread_t waiter;assert(!pthread_create(&waiter,NULL,join_copy,NULL));
    while(!__atomic_load_n(&join_entered,__ATOMIC_ACQUIRE))usleep(100);
    usleep(1000);assert(!__atomic_load_n(&join_done,__ATOMIC_ACQUIRE));
    __atomic_store_n(&pause_worker,0,__ATOMIC_RELEASE);assert(!pthread_join(waiter,NULL));
    for(unsigned s=0;s<3;s++) {
        xv_vertex_upload_wait(s);expect_write(s,1,0,65536);exact_slot(s,131072);
    }
    if(reset)assert(!pools[1].used && !pools[1].has_ticket && !pools[1].dirty_end);
    /* Only explicitly reused slot1 changes; slots0/2 remain retained. */
    xv_vertex_upload_reset(1);clear_trace();
    assert(xv_vertex_upload(1,expected[1],131072));
    xv_vertex_upload_seal(1);xv_vertex_upload_wait(1);
    for(unsigned s=0;s<3;s++){expect_write(s,0,0,0);exact_slot(s,131072);}
    reset_worker();
}
static void full_queue_falls_back(void)
{
    seed(0,65536);prime(0,65536);clear_trace();
    unsigned char a[64]={0},b[UPLOAD_JOBS][64];uint32_t last;
    __atomic_store_n(&pause_worker,1,__ATOMIC_RELEASE);
    for(unsigned i=0;i<UPLOAD_JOBS;i++)assert(xv_upload_worker_submit(b[i],a,64,&last));
    source[0][3][0]^=9;expected[0][3*8192]^=9;
    for(unsigned i=0;i<8;i++)assert(xv_vertex_upload(0,source[0][i],8192));
    xv_vertex_upload_seal(0);assert(!pools[0].has_ticket);
    /* Its own disjoint envelope completes on caller; unrelated jobs stay queued. */
    expect_write(0,1,3*8192,8192);exact_slot(0,65536);
    __atomic_store_n(&pause_worker,0,__ATOMIC_RELEASE);xv_upload_worker_wait(last);reset_worker();
}
int main(void)
{
    assert(!resident_enabled()); /* Repository default stays OFF. */
    const unsigned masks[]={0,1,128,0x18,0x42,255};
    for(unsigned i=0;i<sizeof masks/sizeof *masks;i++)envelope(masks[i],1,0);
    envelope(0,0,0);envelope(0x42,0,0); /* OFF retains original full-range copies. */
    for(unsigned fail=1;fail<=4;fail++)envelope(0x18,1,fail);
    padding_and_stale_prefix();dirty_changes_after_resident_hit();
    clean_tail_retains_ticket(0);clean_tail_retains_ticket(1);full_queue_falls_back();
    xv_vertex_upload_override(-1);xv_vertex_worker_override(-1);
    assert(!resident_enabled() && !xv_vertex_worker_enabled());
    puts("PASS actual GPU copy ranges: clean/dirty envelopes, internal gaps, OFF identity, padding/stale prefix, same-frame mutations, delayed zero/wrapped tickets, three slots, reset, start/signal/full-queue fallback");
    return 0;
}
