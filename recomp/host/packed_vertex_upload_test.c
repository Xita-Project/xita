/* Real allocator and worker; fetched-byte oracle uses independent scalar
 * addresses, including every record retained across source mutations. */
#define XV_PACKED_VERTEX_LAYOUT 1
#define XV_UPLOAD_WORKER_TEST_NO_MAIN
#include "upload_worker_test.c"
static uint8_t input[3][131072],saved[3][131072];
static void fetched(const void *p,const uint8_t *raw,unsigned n,unsigned stride)
{
    const uint8_t *q=p;
    for(unsigned i=0;i<n;i++)assert(!memcmp(q+i*stride,raw+i*32,16));
}
static void seed_inputs(void)
{
    for(unsigned s=0;s<3;s++)for(unsigned i=0;i<sizeof input[s];i++)
        input[s][i]=saved[s][i]=(uint8_t)(i*13+(i/32)*17+s*31);
}
static void mutation_and_layout(unsigned worker)
{
    seed_inputs();xv_vertex_worker_override(worker);xv_vertex_upload_override(1);
    const uint8_t *a=xv_vertex_upload_packed(0,input[0]+1,513);
    assert(a && xv_vertex_upload_packed(0,input[0]+1,513)==a);
    /* An unused tail changes no fetched byte. A used prefix appends. */
    input[0][1+17]^=91;assert(xv_vertex_upload_packed(0,input[0]+1,513)==a);
    input[0][1+32*512+15]^=99;
    const uint8_t *b=xv_vertex_upload_packed(0,input[0]+1,513);assert(b && b!=a);
    const uint8_t *raw=xv_vertex_upload(0,input[0]+1,513*32);assert(raw && raw!=a && raw!=b);
    assert(xv_vertex_upload_packed(0,input[0]+1,513)==b);
    /* Shorter requests may share this representation only. */
    assert(xv_vertex_upload_packed(0,input[0]+1,3)==b);
    uint8_t latest[513*32];memcpy(latest,input[0]+1,sizeof latest);
    memset(input[0],0,sizeof input[0]);
    xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
    fetched(a,saved[0]+1,513,16);fetched(b,latest,513,16);fetched(raw,latest,513,32);
    assert(!memcmp(pools[0].cpu,pools[0].gpu,pools[0].valid_bytes));reset_worker();
}
static void slots_and_pending(int fail)
{
    seed_inputs();xv_vertex_worker_override(0);xv_vertex_upload_override(1);
    for(unsigned s=0;s<3;s++) {
        assert(xv_vertex_upload_packed(s,input[s],4096));xv_vertex_upload_reset(s);
    }
    xv_vertex_worker_override(1);submitted=completed=UINT32_MAX;
    __atomic_store_n(&pause_worker,1,__ATOMIC_RELEASE);
    if(fail)fail_signal=1;
    /* A leading dirty run, followed by an all-clean tail, must retain the
     * real pending ticket, including ticket zero after wrap. */
    input[0][0]^=33;saved[0][0]^=33;
    assert(xv_vertex_upload_packed(0,input[0],256));xv_vertex_upload_seal(0);
    int pending_ticket=pools[0].has_ticket;
    uint32_t ticket=pools[0].ticket;
    assert(xv_vertex_upload_packed(0,input[0]+256*32,4096-256));xv_vertex_upload_seal(0);
    assert(pools[0].has_ticket==pending_ticket && pools[0].ticket==ticket);
    if(!fail)assert(pending_ticket && ticket==0);
    for(unsigned s=1;s<3;s++) {
        assert(xv_vertex_upload_packed(s,input[s],4096));xv_vertex_upload_seal(s);
        fetched(pools[s].gpu,saved[s],4096,16);
    }
    memset(input,0,sizeof input); /* queued copy can no longer consult guest */
    __atomic_store_n(&pause_worker,0,__ATOMIC_RELEASE);
    for(unsigned s=0;s<3;s++) {
        xv_vertex_upload_wait(s);fetched(pools[s].gpu,saved[s],4096,16);
        xv_vertex_upload_reset(s); /* fixture has completed this slot's GPU use */
        const uint8_t *raw=xv_vertex_upload(s,saved[s],4096*32);
        assert(raw);xv_vertex_upload_seal(s);xv_vertex_upload_wait(s);
        fetched(raw,saved[s],4096,32);
        xv_vertex_upload_reset(s);
        const uint8_t *packed=xv_vertex_upload_packed(s,saved[s],4096);
        assert(packed);xv_vertex_upload_seal(s);xv_vertex_upload_wait(s);
        fetched(packed,saved[s],4096,16);
    }
    reset_worker();
}
static void raw_padding(void)
{
    seed_inputs();xv_vertex_worker_override(1);xv_vertex_upload_override(1);
    for(unsigned frame=0;frame<2;frame++) {
        assert(xv_vertex_upload(0,input[0],17));
        const uint8_t *p=xv_vertex_upload_packed(0,input[0]+32,3);assert(p==pools[0].gpu+32);
        xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
        for(unsigned i=17;i<32;i++)assert(!pools[0].gpu[i] && !pools[0].cpu[i]);
        fetched(p,input[0]+32,3,16);xv_vertex_upload_reset(0);
    }
    reset_worker();
}
int main(void)
{
    for(unsigned w=0;w<2;w++)mutation_and_layout(w);
    slots_and_pending(0);slots_and_pending(1);
    raw_padding();
    seed_inputs();xv_vertex_worker_override(0);
    for(unsigned fail=1;fail<=3;fail++) {
        fail_at=calls+fail;
        assert(!xv_vertex_upload_packed(0,input[0],3));
        assert(!live);fail_at=0;reset_worker();
    }
    assert(!xv_vertex_upload_packed(3,input[0],1));
    assert(!xv_vertex_upload_packed(0,input[0],0));
    assert(!xv_vertex_upload_packed(0,input[0],UINT32_MAX));
    puts("PASS packed uploads: live-byte mutation, tail mutation, raw/packed keys, unaligned aliases, short reuse, delayed copies, clean tails, failure, wrap and three-slot reuse");
}
