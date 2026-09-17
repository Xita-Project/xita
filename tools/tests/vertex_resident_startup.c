/* Fresh-process selection through the actual uploader and upload worker. */
#define XV_VERTEX_UPLOAD_TEST_LOG
#define XV_UPLOAD_WORKER_TEST_NO_MAIN
#include "../../recomp/host/upload_worker_test.c"
#include <stdarg.h>

static unsigned startup_logs, mode_logs;
static int startup_mode, startup_default, report_mode;
void xv_logf(const char *fmt, ...)
{
    va_list ap;va_start(ap,fmt);
    if (strstr(fmt,"[vertex-resident] startup mode")) {
        startup_logs++;startup_mode=va_arg(ap,int);startup_default=va_arg(ap,int);
    } else if (strstr(fmt,"[vertex-resident] %u frames")) {
        assert(va_arg(ap,unsigned)==1);mode_logs++;report_mode=va_arg(ap,int);
    }
    va_end(ap);
}

static unsigned char source[4097];
static void *retained;
static int asynchronous;
static void next_generation(int resident)
{
    xv_vertex_upload_reset(0);
    unsigned before=copies, hits=resident_hits, jobs=submitted;
    uint64_t queued=gpu_queued_bytes, caller=gpu_caller_bytes;
    assert(xv_vertex_upload(0,source,sizeof source)==retained);
    assert(pools[0].asynchronous==asynchronous);
    xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
    assert(copies==before+(unsigned)!resident);
    assert(resident_hits==hits+(unsigned)resident);
    assert(submitted==jobs+(unsigned)(asynchronous && !resident));
    assert(gpu_queued_bytes==queued+(asynchronous && !resident?4112u:0u));
    assert(gpu_caller_bytes==caller+(!asynchronous && !resident?sizeof source:0u));
    assert(!memcmp(retained,source,sizeof source));
    assert(!memcmp(pools[0].cpu,pools[0].gpu,pools[0].valid_bytes));
    for(unsigned i=sizeof source;i<4112;i++)assert(!pools[0].gpu[i]);
    assert(startup_logs==1);
}

int main(int argc,char **argv)
{
    assert(argc==4);
    int configured=atoi(argv[1]);asynchronous=atoi(argv[2]);
    int initial_override=atoi(argv[3]);
    assert(resident_override==-1 && !startup_logs && !mode_logs);
    /* The ordinary case deliberately makes no residency setter call. */
    if(initial_override>=0)xv_vertex_upload_override(initial_override);
    int initial=initial_override<0?configured:!!initial_override;
    for(unsigned i=0;i<sizeof source;i++)source[i]=(unsigned char)(i*17u+3u);
    retained=(void *)xv_vertex_upload(0,source,sizeof source);assert(retained);
    assert(startup_logs==1 && startup_mode==initial);
    assert(startup_default==XV_VERTEX_RESIDENT_DEFAULT);
    assert(resident_override==(initial_override<0?-1:!!initial_override));
    assert(copies==1 && !resident_hits && pools[0].asynchronous==asynchronous);
    xv_vertex_upload_seal(0);xv_vertex_upload_wait(0);
    assert(!memcmp(retained,source,sizeof source));
    assert(submitted==(unsigned)asynchronous);
    next_generation(initial); /* Same retained bytes; actual hit/copy proves selection. */

    assert(!setenv("XV_VERTEX_RESIDENT",configured?"0":"1",1));
    next_generation(initial); /* Lazy configuration does not reread the environment. */
    xv_vertex_upload_override(0);next_generation(0);
    xv_vertex_upload_override(1);next_generation(1);
    xv_vertex_upload_override(2);next_generation(1);
    xv_vertex_upload_override(-7);next_generation(configured);
    xv_vertex_upload_report(1);
    assert(startup_logs==1 && mode_logs==1 && report_mode==configured);
    reset_worker();
    printf("PASS build=%d configured=%d initial=%d worker=%d: actual uploads, retained bytes, padding, lazy config, overrides and mode logs\n",
        XV_VERTEX_RESIDENT_DEFAULT,configured,initial,asynchronous);
    return 0;
}
