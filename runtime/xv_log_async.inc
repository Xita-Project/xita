/* Included by xv_log.c. Queue locks protect ownership/counters only; no
 * formatting, output, syscall wait or join executes under this lock. */
#ifdef XV_PROFILE_ASYNC_REPORT
#define LOG_SLOTS 4u
typedef struct {
    char bytes[XV_LOG_REPORT_BYTES];
    unsigned length, console_offset, file_offset, frame, chunk, final;
    uint64_t sequence, report, captured_us;
} log_slot;
static log_slot log_queue[LOG_SLOTS];
static unsigned log_guard, log_stop_guard, log_users, log_quit, log_exited, log_draining;
static SceUID log_thread=-1, log_wake=-1, log_changed=-1;
static xv_log_status log_status;
static uint64_t log_sync_request, log_sync_done, log_sync_target;
static uint64_t log_report_id;
static unsigned log_report_frame, log_report_chunk;

static void queue_lock(void)
{ while (__atomic_exchange_n(&log_guard,1,__ATOMIC_ACQUIRE)) sceKernelDelayThread(10); }
static void queue_unlock(void) { __atomic_store_n(&log_guard,0,__ATOMIC_RELEASE); }
static uint64_t log_now(void) { return sceKernelGetProcessTimeWide(); }
/* Semaphores are wake hints, not ownership proofs. Saturation or a failed
 * signal cannot lose data: timed waits recheck the authoritative counters. */
static void log_hint(SceUID sem) { if(sem>=0) (void)sceKernelSignalSema(sem,1); }
static void log_wait_hint(SceUID sem)
{ unsigned us=1000; if(sem>=0) (void)sceKernelWaitSema(sem,1,&us); else sceKernelDelayThread(us); }
static void log_wait_idle(void)
{ unsigned us=100000; (void)sceKernelWaitSema(log_wake,1,&us); }
static int log_expired(uint64_t start,unsigned limit) { return log_now()-start>=limit; }
static int async_is_worker(void)
{
    queue_lock();
    int same=log_status.state!=XV_LOG_STARTING && sceKernelGetThreadId()==log_thread;
    queue_unlock(); return same;
}

static void log_failure(int error,uint64_t sequence,unsigned console,unsigned file)
{
    queue_lock();
    log_status.error=error ? error : XV_LOG_IO;
    log_status.failed_sequence=sequence;
    log_status.failed_console_offset=console; log_status.failed_file_offset=file;
    log_status.state=XV_LOG_ERROR;
    queue_unlock();
    log_hint(log_changed);
    /* Consumer failures never recurse through the queue or acquire the sink.
     * Remote status remains usable even if the console itself is stalled. */
    sceClibPrintf("[log-worker] output failed %08X at sequence %llu console %u file %u; pending bytes retained\n",
        error,(unsigned long long)sequence,console,file);
}

static int log_writer(SceSize args,void *argp)
{
    (void)args; (void)argp;
    for(;;) {
        queue_lock();
        if(log_quit) { log_exited=1; queue_unlock(); log_hint(log_changed); return 0; }
        int failed=log_status.state==XV_LOG_ERROR;
        uint64_t sync=log_sync_request, target=log_sync_target;
        unsigned queued=log_status.queued;
        log_slot *s=queued ? &log_queue[log_status.written%LOG_SLOTS] : NULL;
        int do_sync=!failed && sync>log_sync_done && log_status.written>=target;
        uint64_t synced=log_status.written, synced_bytes=log_status.written_bytes;
        queue_unlock();
        if(failed || (!queued && !do_sync)) { log_wait_idle(); continue; }
        if(do_sync) {
            uint64_t wait=0,io=0;
            int rc=log_sync(&wait,&io);
            queue_lock(); log_status.file_wait_us+=wait; log_status.sync_us+=io;
            if(!rc) { log_sync_done=sync; log_status.synced=synced; log_status.synced_bytes=synced_bytes; }
            queue_unlock();
            if(rc) log_failure(rc,synced,0,0);
            else log_hint(log_changed);
            continue;
        }
        /* Head stays owned by the consumer through partial writes and errors.
         * No slot or caller buffer can be recycled before written advances. */
        unsigned console=s->console_offset,file=s->file_offset,old_file=file;
        uint64_t console_us=0,wait=0,io=0;
        int rc=log_console(s->bytes,s->length,&console,&console_us);
        if(!rc) rc=log_file(s->bytes,s->length,&file,&wait,&io);
        queue_lock();
        s->console_offset=console; s->file_offset=file;
        log_status.console_us+=console_us; log_status.file_wait_us+=wait; log_status.file_us+=io;
        log_status.written_bytes+=file-old_file;
        if(!rc) {
            log_status.written=s->sequence; log_status.queued--;
            if(s->final) log_status.completed_report=s->report;
        }
        queue_unlock();
        if(rc) log_failure(rc,s->sequence,console,file);
        else log_hint(log_changed);
    }
}

int xv_log_async_start(void)
{
    const char *e=getenv("XV_PROFILE_ASYNC_REPORT");
    if(!e || strcmp(e,"1")) return XV_LOG_UNAVAILABLE;
    queue_lock();
    if(log_status.state!=XV_LOG_SYNC || __atomic_load_n(&g_report_owner,__ATOMIC_ACQUIRE)) {
        int rc=log_status.state==XV_LOG_RUNNING ? XV_LOG_OK : XV_LOG_BUSY;
        queue_unlock(); return rc;
    }
    log_status.state=XV_LOG_STARTING; queue_unlock();
    /* Initialize/rotate on the native startup caller before publication. */
    unsigned offset=0; uint64_t wait=0,io=0;
    int error=log_file("",0,&offset,&wait,&io);
    if(error || g_mtx<0) { if(!error) error=g_mtx; goto fail; }
    log_wake=sceKernelCreateSema("xv_log_wake",0,0,1,NULL);
    if(log_wake<0) { error=log_wake; goto fail; }
    log_changed=sceKernelCreateSema("xv_log_changed",0,0,1,NULL);
    if(log_changed<0) { error=log_changed; goto fail; }
    log_thread=sceKernelCreateThread("xv_periodic_log",log_writer,0x10000120,
        64*1024,0,SCE_KERNEL_CPU_MASK_USER_ALL,NULL);
    if(log_thread<0) { error=log_thread; goto fail; }
    error=sceKernelStartThread(log_thread,0,NULL);
    if(error<0) goto fail;
    queue_lock(); log_status.state=XV_LOG_RUNNING; queue_unlock();
    log_hint(log_wake); return XV_LOG_OK;
fail:
    if(log_thread>=0) sceKernelDeleteThread(log_thread);
    if(log_changed>=0) sceKernelDeleteSema(log_changed);
    if(log_wake>=0) sceKernelDeleteSema(log_wake);
    log_thread=log_changed=log_wake=-1;
    queue_lock(); log_status.startup_error=error ? error : XV_LOG_UNAVAILABLE;
    log_status.state=XV_LOG_SYNC; queue_unlock();
    return XV_LOG_UNAVAILABLE;
}

static int async_report_begin(unsigned frame)
{
    queue_lock();
    int active=log_status.state==XV_LOG_RUNNING || log_status.state==XV_LOG_ERROR;
    int blocked=log_status.state==XV_LOG_STARTING || log_status.state==XV_LOG_DRAINING || log_draining;
    if(active) { ++log_report_id; log_report_frame=frame; log_report_chunk=0; }
    queue_unlock();
    return blocked ? -1 : active;
}

static int async_enqueue(const char *bytes,unsigned length,unsigned final,unsigned timeout_us)
{
    uint64_t began=0,start=log_now();
    for(;;) {
        queue_lock();
        if(log_status.queued<LOG_SLOTS) {
            log_slot *s=&log_queue[log_status.accepted%LOG_SLOTS];
            memcpy(s->bytes,bytes,length);
            s->length=length; s->console_offset=s->file_offset=0;
            s->sequence=++log_status.accepted; s->report=log_report_id;
            s->frame=log_report_frame; s->chunk=log_report_chunk++; s->final=final;
            s->captured_us=log_now();
            log_status.accepted_bytes+=length; log_status.queued++;
            if(log_status.queued>log_status.high_water) log_status.high_water=log_status.queued;
            if(began) log_status.backpressure_us+=log_now()-began;
            queue_unlock(); log_hint(log_wake); return XV_LOG_OK;
        }
        if(!began) { began=log_now(); log_status.backpressure_count++; }
        int failed=log_status.error;
        queue_unlock();
        if(timeout_us && (failed || log_expired(start,timeout_us))) {
            queue_lock(); log_status.backpressure_us+=log_now()-began; queue_unlock();
            return failed ? XV_LOG_IO : XV_LOG_TIMEOUT;
        }
        /* The admitted report retains its builder/source until accepted. On a
         * persistent error it intentionally waits for explicit retry; shutdown
         * times out with all slots and the producer still alive. */
        log_wait_hint(log_changed);
    }
}

void xv_log_get_status(xv_log_status *out)
{
    if(!out) return;
    queue_lock(); *out=log_status;
    if(log_status.queued) {
        const log_slot *s=&log_queue[log_status.written%LOG_SLOTS];
        out->pending_report=s->report; out->pending_captured_us=s->captured_us;
        out->pending_frame=s->frame; out->pending_chunk=s->chunk; out->pending_length=s->length;
    }
    queue_unlock();
    out->open_report=__atomic_load_n(&g_report_owner,__ATOMIC_ACQUIRE)>0;
    if(!out->error) out->error=__atomic_load_n(&g_immediate_error,__ATOMIC_ACQUIRE);
}

static int async_barrier(unsigned timeout_us,int stopping)
{
    uint64_t start=log_now();
    queue_lock();
    if(log_status.state==XV_LOG_STARTING) { queue_unlock(); return XV_LOG_BUSY; }
    if(sceKernelGetThreadId()==log_thread) { queue_unlock(); return XV_LOG_SELF; }
    if(!stopping && log_status.state==XV_LOG_DRAINING) { queue_unlock(); return XV_LOG_BUSY; }
    if(log_thread<0 || log_status.state==XV_LOG_STOPPED || log_status.state==XV_LOG_SYNC) {
        queue_unlock(); return XV_LOG_UNAVAILABLE;
    }
    ++log_users;
    uint64_t request=0,target=log_status.accepted;
    queue_unlock();
    int result=XV_LOG_OK;
    for(;;) {
        queue_lock();
        if(!request && log_sync_request==log_sync_done) {
            request=++log_sync_request; log_sync_target=target;
        }
        int done=request && log_sync_done>=request, error=log_status.error;
        queue_unlock();
        log_hint(log_wake);
        if(error) { result=XV_LOG_IO; break; }
        if(done) break;
        if(log_expired(start,timeout_us)) { result=XV_LOG_TIMEOUT; break; }
        log_wait_hint(log_changed);
    }
    queue_lock(); --log_users; queue_unlock();
    return result;
}

int xv_log_retry(void)
{
    queue_lock();
    if(log_status.state!=XV_LOG_ERROR || log_thread<0 || log_quit) { queue_unlock(); return XV_LOG_BUSY; }
    ++log_users; log_status.error=0; log_status.state=log_draining ? XV_LOG_DRAINING : XV_LOG_RUNNING;
    queue_unlock(); log_hint(log_wake);
    queue_lock(); --log_users; queue_unlock();
    return XV_LOG_OK;
}

int xv_log_shutdown(unsigned timeout_us)
{
    uint64_t start=log_now();
    if(__atomic_exchange_n(&log_stop_guard,1,__ATOMIC_ACQUIRE)) return XV_LOG_BUSY;
    int result=XV_LOG_OK;
    queue_lock();
    if(log_status.state==XV_LOG_STARTING) { queue_unlock(); result=XV_LOG_BUSY; goto out; }
    if(sceKernelGetThreadId()==log_thread) { queue_unlock(); result=XV_LOG_SELF; goto out; }
    if(__atomic_load_n(&g_report_owner,__ATOMIC_ACQUIRE)==sceKernelGetThreadId()) {
        queue_unlock(); result=XV_LOG_BUSY; goto out;
    }
    if(log_thread<0) { queue_unlock(); result=xv_log_flush_wait(timeout_us); goto out; }
    log_draining=1;
    if(log_status.error) { queue_unlock(); result=XV_LOG_IO; goto out; }
    log_status.state=XV_LOG_DRAINING;
    queue_unlock();
    for(;;) {
        queue_lock(); unsigned users=log_users; queue_unlock();
        if(!users && !__atomic_load_n(&g_report_owner,__ATOMIC_ACQUIRE)) break;
        if(log_expired(start,timeout_us)) { result=XV_LOG_TIMEOUT; goto out; }
        log_wait_hint(log_changed);
    }
    if(!log_quit) {
        uint64_t elapsed=log_now()-start;
        unsigned left=elapsed>=timeout_us ? 0 : timeout_us-(unsigned)elapsed;
        result=async_barrier(left,1);
        if(result) goto out;
        queue_lock(); log_quit=1; queue_unlock(); log_hint(log_wake);
    }
    for(;;) {
        queue_lock(); unsigned done=log_exited; queue_unlock();
        if(done) break;
        if(log_expired(start,timeout_us)) { result=XV_LOG_TIMEOUT; goto out; }
        log_wait_hint(log_changed);
    }
    /* Exit acknowledgement means no new work can enter a syscall. The join
     * still precedes deletion; no producer/waiter can observe deleted handles. */
    { unsigned left=1000;
      if(sceKernelWaitThreadEnd(log_thread,NULL,&left)<0) { result=XV_LOG_TIMEOUT; goto out; } }
    sceKernelDeleteThread(log_thread); sceKernelDeleteSema(log_changed); sceKernelDeleteSema(log_wake);
    queue_lock(); log_thread=log_changed=log_wake=-1; log_status.state=XV_LOG_STOPPED; log_draining=0; queue_unlock();
out:
    __atomic_store_n(&log_stop_guard,0,__ATOMIC_RELEASE); return result;
}
#else
static int async_report_begin(unsigned frame) { (void)frame; return 0; }
static int async_enqueue(const char *bytes,unsigned length,unsigned final,unsigned timeout_us)
{ (void)bytes; (void)length; (void)final; (void)timeout_us; return XV_LOG_UNAVAILABLE; }
static int async_is_worker(void) { return 0; }
static int async_barrier(unsigned timeout_us,int stopping)
{ (void)timeout_us; (void)stopping; return XV_LOG_UNAVAILABLE; }
int xv_log_async_start(void) { return XV_LOG_UNAVAILABLE; }
void xv_log_get_status(xv_log_status *out)
{ if(out) { memset(out,0,sizeof *out); out->error=__atomic_load_n(&g_immediate_error,__ATOMIC_ACQUIRE); } }
int xv_log_retry(void) { return XV_LOG_UNAVAILABLE; }
int xv_log_shutdown(unsigned timeout_us) { return xv_log_flush_wait(timeout_us); }
#endif
