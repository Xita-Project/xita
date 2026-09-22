/* Console + fixed append-only file. Only explicitly scoped periodic report
 * bytes may use the optional native writer; ordinary errors stay immediate. */
#ifdef __vita__
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include "xv_log.h"
/* Report ownership is the REAL thread: under the scene overlap the helper (aliased as the owner by
 * xv_owner_thread_id) and the owner ran at once, both appended to g_report unsynchronized, and every other thread
 * then blocked in the logger (perf88-92: the remote server went silent right after the scene thread started). */

static SceUID g_fd=-2, g_mtx=-1;
static unsigned g_init, g_sink_fallback;
static int g_immediate_error;
#define XV_LOG_REPORT_BYTES 32768u
static char g_report[XV_LOG_REPORT_BYTES];
static unsigned g_report_used;
static SceUID g_report_owner;
static int g_report_async;

static void log_initialize(void)
{
    unsigned expected=0;
    if(__atomic_compare_exchange_n(&g_init,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
        g_mtx=sceKernelCreateMutex("xv_log",0,0,NULL);
        __atomic_store_n(&g_init,2,__ATOMIC_RELEASE);
    } else while(__atomic_load_n(&g_init,__ATOMIC_ACQUIRE)!=2) sceKernelDelayThread(10);
}
static int sink_lock(void)
{
    log_initialize();
    if(g_mtx>=0) return sceKernelLockMutex(g_mtx,1,NULL);
    /* Creation failure preserves serialized synchronous output. This is a
     * sink-only lock, never the queue/lifecycle lock. No worker is started. */
    while(__atomic_exchange_n(&g_sink_fallback,1,__ATOMIC_ACQUIRE)) sceKernelDelayThread(100);
    return 0;
}
static void sink_unlock(void)
{
    if(g_mtx>=0) (void)sceKernelUnlockMutex(g_mtx,1);
    else __atomic_store_n(&g_sink_fallback,0,__ATOMIC_RELEASE);
}
static void log_open(void)
{
    if(g_fd!=-2) return;
    sceIoMkdir("ux0:data",0777);
    SceIoStat st;
    if(sceIoGetstat("ux0:data/xita",&st)<0 && sceIoGetstat("ux0:data/xboxvita",&st)>=0 &&
       sceIoRename("ux0:data/xboxvita","ux0:data/xita")>=0)
        sceIoRename("ux0:data/xita/xboxvita.cfg","ux0:data/xita/xita.cfg");
    sceIoMkdir("ux0:data/xita",0777);
    sceIoRemove("ux0:data/xita/xita.3.log");
    sceIoRename("ux0:data/xita/xita.2.log","ux0:data/xita/xita.3.log");
    sceIoRename("ux0:data/xita/xita.1.log","ux0:data/xita/xita.2.log");
    sceIoRename("ux0:data/xita/xita.log","ux0:data/xita/xita.1.log");
    g_fd=sceIoOpen("ux0:data/xita/xita.log",SCE_O_WRONLY|SCE_O_CREAT|SCE_O_TRUNC,0777);
}
static uint64_t sink_now(void)
{
#ifdef XV_PROFILE_ASYNC_REPORT
    return sceKernelGetProcessTimeWide();
#else
    return 0;
#endif
}
static int log_console(const char *buf,unsigned n,unsigned *offset,uint64_t *elapsed)
{
    uint64_t start=sink_now(); int error=0;
    while(*offset<n) {
        unsigned chunk=n-*offset;
        if(chunk>511) chunk=511;
        for(unsigned i=chunk;i;--i) if(buf[*offset+i-1]=='\n') { chunk=i; break; }
        int r=sceClibPrintf("%.*s",(int)chunk,buf+*offset);
        /* sceClibPrintf is not a byte-counted write: a successful console
         * implementation may return zero or another nonnegative status. */
        if(r<0) { error=r; break; }
        *offset+=chunk;
    }
    *elapsed+=sink_now()-start; return error;
}
static int log_file(const char *buf,unsigned n,unsigned *offset,uint64_t *waiting,uint64_t *elapsed)
{
    uint64_t start=sink_now(); int locked=sink_lock(); uint64_t entered=sink_now();
    if(locked<0) { *waiting+=entered-start; return locked; }
    log_open(); int error=g_fd<0 ? (int)g_fd : 0;
    while(!error && *offset<n) {
        SceSSize r=sceIoWrite(g_fd,buf+*offset,(SceSize)(n-*offset));
        if(r<=0 || (unsigned)r>n-*offset) { error=r<0 ? (int)r : XV_LOG_IO; break; }
        *offset+=(unsigned)r;
    }
    uint64_t ended=sink_now(); sink_unlock();
    *waiting+=entered-start; *elapsed+=ended-entered; return error;
}
static int log_sync(uint64_t *waiting,uint64_t *elapsed)
{
    uint64_t start=sink_now(); int locked=sink_lock(); uint64_t entered=sink_now();
    if(locked<0) { *waiting+=entered-start; return locked; }
    log_open(); int result=g_fd<0 ? XV_LOG_IO : sceIoSyncByFd(g_fd,0);
    uint64_t ended=sink_now(); sink_unlock();
    *waiting+=entered-start; *elapsed+=ended-entered;
    return result<0 ? result : __atomic_load_n(&g_immediate_error,__ATOMIC_ACQUIRE);
}

#include "xv_log_async.inc"

static void log_write_immediate(const char *buf,unsigned n)
{
    unsigned console=0,file=0; uint64_t elapsed=0,waiting=0;
    /* Console stays before the file lock: critical evidence can overtake an
     * in-flight periodic file write. Periodic chunks alone promise FIFO. */
    int c=log_console(buf,n,&console,&elapsed);
    int f=log_file(buf,n,&file,&waiting,&elapsed);
    if(c || f) __atomic_store_n(&g_immediate_error,c ? c : f,__ATOMIC_RELEASE);
}
static int report_is_owner(void)
{
    SceUID owner=__atomic_load_n(&g_report_owner,__ATOMIC_ACQUIRE);
    return owner>0 && owner==sceKernelGetThreadId();
}
static int report_begin(unsigned frame,int async_only)
{
    SceUID expected=0,current=sceKernelGetThreadId();
    if(current<=0 || !__atomic_compare_exchange_n(&g_report_owner,&expected,current,
            0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 0;
    g_report_async=async_report_begin(frame);
    if(g_report_async<0 || (async_only && !g_report_async)) {
        __atomic_store_n(&g_report_owner,0,__ATOMIC_RELEASE);return 0;
    }
    g_report_used=0; return 1;
}
int xv_log_report_begin_frame(unsigned frame) { return report_begin(frame,0); }
int xv_log_report_begin(void) { return xv_log_report_begin_frame(0); }
int xv_log_report_begin_async_frame(unsigned frame)
{
    xv_log_status status; xv_log_get_status(&status);
    return status.enabled && report_begin(frame,1);
}
static int report_flush(unsigned final,unsigned timeout_us)
{
    if(g_report_async) {
        if(g_report_used || final) {
            int rc=async_enqueue(g_report,g_report_used,final,timeout_us);
            if(rc) return rc;
        }
    } else if(g_report_used) log_write_immediate(g_report,g_report_used);
    g_report_used=0; return XV_LOG_OK;
}
void xv_log_report_end(void)
{
    if(!report_is_owner()) return;
    (void)report_flush(1,0);
    __atomic_store_n(&g_report_owner,0,__ATOMIC_RELEASE);
}
static unsigned g_helper_dropped;
int xv_scene_thread_on_helper(void) __attribute__((weak));   /* recomp/kernel/xk_scene_thread.c */
void xv_log_write(const char *buf,unsigned n)
{
    if(!buf || !n) return;
    /* The scene helper logs hundreds of shader-binding lines per frame; through the immediate (mutex + file) path they
     * stalled every other logging thread (the remote server died within seconds of the game start, perf93-95). Dropped
     * for now; the count is reported by the scene thread. Critical lines still use xv_log_criticalf. */
    if(xv_scene_thread_on_helper && xv_scene_thread_on_helper()) { __atomic_add_fetch(&g_helper_dropped,1,__ATOMIC_RELAXED); return; }
    if(!report_is_owner()) { log_write_immediate(buf,n); return; }
    if(!g_report_async) {
        /* Preserve the reviewed synchronous grouping/oversized-write policy. */
        if(n>XV_LOG_REPORT_BYTES-g_report_used) (void)report_flush(0,0);
        if(n<=XV_LOG_REPORT_BYTES) { memcpy(g_report+g_report_used,buf,n); g_report_used+=n; }
        else log_write_immediate(buf,n);
        return;
    }
    while(n) {
        if(g_report_used==XV_LOG_REPORT_BYTES) (void)report_flush(0,0);
        unsigned take=XV_LOG_REPORT_BYTES-g_report_used;
        if(take>n) take=n;
        memcpy(g_report+g_report_used,buf,take); g_report_used+=take; buf+=take; n-=take;
    }
}
unsigned xv_log_helper_dropped(void) { return __atomic_exchange_n(&g_helper_dropped,0,__ATOMIC_RELAXED); }
void xv_logf(const char *fmt,...)
{
    char buf[512]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
    if(n<=0) return;
    if(n>(int)sizeof buf-1) n=(int)sizeof buf-1;
    xv_log_write(buf,(unsigned)n);
}
void xv_log_criticalf(const char *fmt,...)
{
    char buf[512]; va_list ap; va_start(ap,fmt);
    int n=vsnprintf(buf,sizeof buf,fmt,ap); va_end(ap);
    if(n<=0) return;
    if(n>(int)sizeof buf-1) n=(int)sizeof buf-1;
    log_write_immediate(buf,(unsigned)n);
}
int xv_log_flush_wait(unsigned timeout_us)
{
    uint64_t began=sink_now();
    if(async_is_worker()) return XV_LOG_SELF;
    SceUID owner=__atomic_load_n(&g_report_owner,__ATOMIC_ACQUIRE);
    if(owner && owner!=sceKernelGetThreadId()) return XV_LOG_BUSY;
    if(owner) { int rc=report_flush(0,timeout_us ? timeout_us : 1); if(rc) return rc; }
    uint64_t elapsed=sink_now()-began;
    timeout_us=elapsed>=timeout_us ? 0 : timeout_us-(unsigned)elapsed;
    int result=async_barrier(timeout_us,0);
    if(result!=XV_LOG_UNAVAILABLE) return result;
    uint64_t waiting=0,sync_elapsed=0;
    return log_sync(&waiting,&sync_elapsed)<0 ? XV_LOG_IO : XV_LOG_OK;
}
void xv_log_flush(void) { (void)xv_log_flush_wait(5000000); }
#endif
