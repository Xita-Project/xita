/* Production uploader and real copy worker. Vita services are the existing
 * pthread fixture; expected GPU fetches come from the retained index list,
 * independently of the candidate bitmap walk. No guest-write epoch is assumed. */
#define XV_UPLOAD_WORKER_TEST_NO_MAIN
#define XV_VERTEX_UPLOAD_TEST_LOG
#include "../../recomp/host/upload_worker_test.c"
#include "../../runtime/xv_index_copy.h"
#include <stdarg.h>
static char selected_report[512];
void xv_logf(const char *format,...)
{
    if (!strstr(format,"[vertex-resident-references]")) return;
    va_list args;va_start(args,format);
    vsnprintf(selected_report,sizeof selected_report,format,args);va_end(args);
}

static unsigned char src[3][131073], old[3][131072];
static unsigned cases;
static int mode;
static void fill(unsigned slot,unsigned bytes)
{
    for(unsigned i=0;i<bytes;i++)old[slot][i]=(unsigned char)(i*31u+i/257u+slot*53u);
    memcpy(src[slot]+1,old[slot],bytes);
}
static void prime_slot(unsigned slot,unsigned bytes)
{
    xv_vertex_worker_override(0);xv_vertex_upload_override(0);
    assert(xv_vertex_upload(slot,old[slot],bytes));
    xv_vertex_upload_reset(slot);
    xv_vertex_upload_override(1);
}
static void join_slot(unsigned slot)
{
    xv_vertex_upload_seal(slot);xv_vertex_upload_wait(slot);
    assert(!memcmp(pools[slot].gpu,pools[slot].cpu,pools[slot].valid_bytes));
}
static xv_vertex_refs capture(const uint16_t *indices,unsigned n,unsigned vertices)
{
    uint16_t retained[32];assert(n<=32);
    xv_vertex_refs refs;
    assert(xv_index_copy_reference_bounds(retained,indices,n,&refs)==vertices);
    assert(!memcmp(retained,indices,n*2));return refs;
}
static void fetched(const unsigned char *gpu,const unsigned char *source,
                    const uint16_t *indices,unsigned n,unsigned stride)
{
    for(unsigned i=0;i<n;i++)assert(!memcmp(gpu+indices[i]*stride,source+indices[i]*stride,stride));
}
static void mutation_and_alias(unsigned worker,unsigned stride,unsigned vertices)
{
    unsigned bytes=vertices*stride;fill(0,bytes);prime_slot(0,bytes);
    xv_vertex_worker_override(worker);
    uint16_t ix[]={0,16,(uint16_t)(vertices-1)};
    xv_vertex_refs refs=capture(ix,3,vertices);
    assert(xv_vertex_refs_sparse(&refs,bytes,stride));
    unsigned char *source=src[0]+1,*alias=source;
    alias[300*stride]^=0x5a; /* Current frame's first request, unused old group. */
    unsigned before=copies,checks=resident_reference_checks,hits=resident_reference_hits;
    const unsigned char *first=xv_vertex_upload_referenced(0,source,bytes,stride,&refs);
    assert(first==pools[0].gpu);join_slot(0);fetched(first,source,ix,3,stride);
    assert(copies-before==(unsigned)!mode);
    assert(resident_reference_checks-checks==(unsigned)mode);
    assert(resident_reference_hits-hits==(unsigned)mode);
    assert(first[300*stride]==(mode?old[0][300*stride]:source[300*stride]));
    unsigned char retained[8192];assert(bytes<=sizeof retained);memcpy(retained,first,bytes);
    /* This same CPU source's later reference must observe the previously
     * unused update, even after appending a sparse retired entry. */
    uint16_t later[]={0,300,(uint16_t)(vertices-1)};
    xv_vertex_refs newrefs=capture(later,3,vertices);
    const unsigned char *second=xv_vertex_upload_referenced(0,source,bytes,stride,&newrefs);
    assert(second && (mode?second!=first:second==first));join_slot(0);fetched(second,source,later,3,stride);
    assert(!memcmp(first,retained,bytes));
    /* A different CPU source with identical current bytes gets no pointer-only
     * admission; remapped resource identity must still capture current values. */
    memcpy(src[1]+1,source,bytes);
    const unsigned char *other=xv_vertex_upload_referenced(0,src[1]+1,bytes,stride,&newrefs);
    assert(other && other!=first && other!=second);join_slot(0);fetched(other,src[1]+1,later,3,stride);
    /* Same pointer with changed fetched bytes must append even after a sparse
     * retired hit; no current-frame metadata grants blanket validity. */
    source[16*stride]^=77;
    const unsigned char *third=xv_vertex_upload_referenced(0,source,bytes,stride,&refs);
    assert(third && third!=first);join_slot(0);fetched(third,source,ix,3,stride);
    assert(!memcmp(first,retained,bytes));
    source[450*stride]^=11;
    const unsigned char *full=xv_vertex_upload(0,source,bytes);
    assert(full && full!=first);join_slot(0);assert(!memcmp(full,source,bytes));
    assert(!memcmp(first,retained,bytes));reset_worker();cases++;
}
static void mask_fallback(unsigned kind)
{
    const unsigned bytes=4096,stride=4;fill(0,bytes);prime_slot(0,bytes);
    uint16_t ix[]={0,1023};xv_vertex_refs refs=capture(ix,2,1024);
    unsigned callbytes=bytes,callstride=stride;const xv_vertex_refs *r=&refs;
    switch(kind) {
    case 0:r=NULL;break;
    case 1:refs.groups=0;break;
    case 2:refs.groups=8193;break;
    case 3:refs.vertices=65537;break;
    case 4:refs.vertices=511;break;
    case 5:callstride=0;break;
    case 6:callbytes--;break;
    case 7:callstride=UINT32_MAX;break;
    case 8:refs.groups=refs.vertices/8;memset(refs.bits,255,sizeof refs.bits);break;
    case 9:xv_vertex_upload_override(0);break;
    }
    src[0][1+300*stride]^=0x5a;
    unsigned checks=resident_reference_checks,before=copies;
    const unsigned char *p=xv_vertex_upload_referenced(0,src[0]+1,callbytes,callstride,r);
    assert(p);join_slot(0);assert(!memcmp(p,src[0]+1,callbytes));
    assert(copies==before+1 && resident_reference_checks==checks);reset_worker();cases++;
}
#if XV_PACKED_VERTEX_LAYOUT
static void packed_is_distinct(void)
{
    fill(0,32768);xv_vertex_worker_override(0);xv_vertex_upload_override(0);
    assert(xv_vertex_upload_packed(0,src[0]+1,1024));xv_vertex_upload_reset(0);
    xv_vertex_upload_override(1);
    unsigned checks=resident_reference_checks;
    const unsigned char *packed=xv_vertex_upload_packed(0,src[0]+1,1024);assert(packed);
    for(unsigned i=0;i<1024;i++)assert(!memcmp(packed+i*16,src[0]+1+i*32,16));
    assert(resident_reference_checks==checks);
    uint16_t ix[]={0,1023};xv_vertex_refs refs=capture(ix,2,1024);
    const unsigned char *raw=xv_vertex_upload_referenced(0,src[0]+1,32768,32,&refs);
    assert(raw && raw!=packed);fetched(raw,src[0]+1,ix,2,32);
    for(unsigned i=0;i<1024;i++)assert(!memcmp(packed+i*16,src[0]+1+i*32,16));
    reset_worker();cases++;
}
#endif
static int waiting,finished;
static void *reset_join(void *unused)
{
    (void)unused;__atomic_store_n(&waiting,1,__ATOMIC_RELEASE);
    xv_vertex_upload_reset(1);
    __atomic_store_n(&finished,1,__ATOMIC_RELEASE);return NULL;
}
static void delayed_three_slots(void)
{
    for(unsigned s=0;s<3;s++){fill(s,131072);prime_slot(s,131072);}
    submitted=completed=UINT32_MAX-1;xv_vertex_worker_override(1);
    __atomic_store_n(&pause_worker,1,__ATOMIC_RELEASE);
    uint16_t ix[]={0,1023};xv_vertex_refs refs=capture(ix,2,1024);
    for(unsigned s=0;s<3;s++) {
        src[s][1]^=19; /* Changed referenced first half: actual pending copy. */
        assert(xv_vertex_upload_referenced(s,src[s]+1,65536,64,&refs));
        uint32_t ticket=pools[s].ticket;assert(pools[s].has_ticket && ticket==(uint32_t)(UINT32_MAX+s));
        /* Unchanged fetched groups but changed unused bytes in the second half.
         * The candidate's clean dispatch must retain the earlier wrapped ticket. */
        src[s][1+65536+300*64]^=41;
        assert(xv_vertex_upload_referenced(s,src[s]+1+65536,65536,64,&refs));
        xv_vertex_upload_seal(s);
        if(mode)assert(pools[s].has_ticket && pools[s].ticket==ticket);
        else { /* Baseline dispatches both halves; reset ticket numbering per mode below. */
            assert(0 && "delayed_three_slots is an enabled-candidate lifecycle fixture");
        }
        memset(src[s],0xee,sizeof src[s]);
    }
    pthread_t waiter;waiting=finished=0;assert(!pthread_create(&waiter,NULL,reset_join,NULL));
    while(!__atomic_load_n(&waiting,__ATOMIC_ACQUIRE))usleep(100);
    usleep(1000);assert(!__atomic_load_n(&finished,__ATOMIC_ACQUIRE));
    __atomic_store_n(&pause_worker,0,__ATOMIC_RELEASE);assert(!pthread_join(waiter,NULL));
    for(unsigned s=0;s<3;s++) {
        xv_vertex_upload_wait(s);old[s][0]^=19;
        assert(!memcmp(pools[s].gpu,old[s],131072));
        assert(!memcmp(pools[s].cpu,pools[s].gpu,pools[s].valid_bytes));
    }
    assert(!pools[1].used && !pools[1].has_ticket);
    /* Test acts as final GPU consumer: only after this check may a slot reset.
     * Changing resource identity/order must validate contents at the new offset. */
    xv_vertex_upload_reset(0);memcpy(src[2]+1,old[0],131072);
    src[2][1+300*64]^=33;
    const unsigned char *p=xv_vertex_upload_referenced(0,src[2]+1,65536,64,&refs);
    assert(p);join_slot(0);assert(!memcmp(p,old[0],65536));
    assert(!memcmp(pools[1].gpu,old[1],131072) && !memcmp(pools[2].gpu,old[2],131072));
    reset_worker();cases++;
}
static void failed_copy_start(unsigned failure)
{
    fill(0,131072);prime_slot(0,131072);xv_vertex_worker_override(1);
    if(failure<=3)init_failure=failure;else fail_signal=1;
    uint16_t ix[]={0,1023};xv_vertex_refs refs=capture(ix,2,1024);
    src[0][1]^=71;old[0][0]^=71;
    assert(xv_vertex_upload_referenced(0,src[0]+1,65536,64,&refs));
    src[0][1+65536+300*64]^=41;
    assert(xv_vertex_upload_referenced(0,src[0]+1+65536,65536,64,&refs));join_slot(0);
    assert(!memcmp(pools[0].gpu,old[0],131072));reset_worker();cases++;
}
int main(int argc,char **argv)
{
    assert(argc==2);mode=atoi(argv[1]);assert(resident_references_enabled()==mode);
    assert(!resident_enabled()); /* Independent repository-default residency. */
    for(unsigned worker=0;worker<2;worker++) {
        mutation_and_alias(worker,4,1024);
        mutation_and_alias(worker,3,1025); /* Partial final group + odd padding. */
    }
    for(unsigned k=0;k<10;k++)mask_fallback(k);
#if XV_PACKED_VERTEX_LAYOUT
    packed_is_distinct();
#endif
    if(mode) {
        delayed_three_slots();
        for(unsigned failure=1;failure<=4;failure++)failed_copy_start(failure);
        assert(resident_reference_checks && resident_reference_hits &&
            resident_reference_compared<resident_reference_requested);
    } else assert(!resident_reference_checks);
    xv_vertex_upload_report(1); /* Actual bounded startup/selected-mode report. */
    char expected_report[96];snprintf(expected_report,sizeof expected_report,
        "[vertex-resident-references] 1 frames: enabled %d;",mode);
    assert(strstr(selected_report,expected_report));fputs(selected_report,stdout);
    assert(!resident_reference_checks && !resident_reference_compared);
    printf("PASS %u production upload cases; selected=%d, packed=%d\n",cases,mode,XV_PACKED_VERTEX_LAYOUT);
    return 0;
}
