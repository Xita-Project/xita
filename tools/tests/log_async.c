/* Exercise the actual Vita queue/sink using real host threads and deterministic
 * blocked syscalls. No filesystem or device is accessed. Run each case in a
 * fresh process, matching the once-per-process production lifecycle. */
#define __vita__ 1
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>
#include "../../runtime/xv_log.c"

#define CAP (16u*1024u*1024u)
static char output[CAP],console_output[CAP],oracle[CAP];
static size_t output_n,console_n,oracle_n;
static pthread_mutex_t io=PTHREAD_MUTEX_INITIALIZER,sink=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static _Thread_local SceUID identity=1;
static atomic_int creates,opens,deleted_threads,deleted_semas,writer_entered,console_entered;
static atomic_int file_calls,writer_file_calls,sync_calls,ordinary_entered;
static int ordinary_block,ordinary_permits;
static int fail_init,write_block,write_permits,console_block,console_permits,short_write;
static int fail_after=-1,write_error=-77,sync_error,console_fail_after=-1,console_error=-78;
static int console_status=-1;
static _Thread_local unsigned deadline_clock,deadline_reads;
static int self_check;
static int join_timeouts;
static atomic_int failed_hints;
static atomic_int self_checked;
static SceKernelThreadEntry native_entry;
static pthread_t native_thread;
static int native_started;
typedef struct { pthread_mutex_t mutex; pthread_cond_t cond; int count,deleted,waiters; } sema;
static sema sems[2]; static unsigned sem_count;

SceUID sceKernelGetThreadId(void) { return identity; }
SceUInt64 sceKernelGetProcessTimeWide(void)
{
    /* Cross a 10 ms shutdown deadline precisely between successive clock
     * reads; the writer keeps its real clock. No scheduling luck is required. */
    if(deadline_clock) {
        unsigned read=deadline_reads++;
        return read==0 ? 1000 : read==1 ? 10999 : 11001+(uint64_t)(read-2)*1000;
    }
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000+t.tv_nsec/1000;
}
int sceKernelDelayThread(SceUInt us)
{ struct timespec t={us/1000000,(long)(us%1000000)*1000}; nanosleep(&t,NULL); return 0; }
SceUID sceKernelCreateMutex(const char *name,SceUInt attr,int initial,SceKernelMutexOptParam *opt)
{ (void)name;(void)attr;(void)initial;(void)opt; atomic_fetch_add(&creates,1); return fail_init==1 ? -31 : 20; }
int sceKernelLockMutex(SceUID uid,int count,unsigned *timeout)
{ assert(uid==20 && count==1 && !timeout); return pthread_mutex_lock(&sink); }
int sceKernelUnlockMutex(SceUID uid,int count)
{ assert(uid==20 && count==1); return pthread_mutex_unlock(&sink); }
SceUID sceKernelCreateSema(const char *name,SceUInt attr,int initial,int maximum,SceKernelSemaOptParam *opt)
{
    (void)name;(void)attr;(void)opt;assert(initial==0 && maximum==1 && sem_count<2);
    if(fail_init==2+(int)sem_count)return -32;
    sema *s=&sems[sem_count];pthread_mutex_init(&s->mutex,NULL);pthread_cond_init(&s->cond,NULL);
    return 30+(int)sem_count++;
}
int sceKernelDeleteSema(SceUID uid)
{
    assert(uid>=30 && uid<32); sema *s=&sems[uid-30];pthread_mutex_lock(&s->mutex);
    assert(!s->deleted && !s->waiters);s->deleted=1;pthread_mutex_unlock(&s->mutex);
    atomic_fetch_add(&deleted_semas,1);return 0;
}
int sceKernelSignalSema(SceUID uid,int count)
{
    assert(uid>=30 && uid<32 && count==1); sema *s=&sems[uid-30];pthread_mutex_lock(&s->mutex);
    if(atomic_load(&failed_hints)) { pthread_mutex_unlock(&s->mutex);return -33; }
    assert(!s->deleted);int rc=s->count ? -1 : 0;s->count=1;pthread_cond_broadcast(&s->cond);
    pthread_mutex_unlock(&s->mutex);return rc;
}
int sceKernelWaitSema(SceUID uid,int count,unsigned *timeout)
{
    assert(uid>=30 && uid<32 && count==1 && timeout);sema *s=&sems[uid-30];
    /* The scripted deadline is already expired. A second clock read in the
     * remaining-budget subtraction used to underflow and reach this wait. */
    assert(!deadline_clock);
    struct timespec t;clock_gettime(CLOCK_REALTIME,&t);t.tv_nsec+=(long)*timeout*1000;
    t.tv_sec+=t.tv_nsec/1000000000;t.tv_nsec%=1000000000;
    pthread_mutex_lock(&s->mutex);assert(!s->deleted);s->waiters++;
    int rc=0;while(!s->count && !rc)rc=pthread_cond_timedwait(&s->cond,&s->mutex,&t);
    if(s->count)s->count--;s->waiters--;pthread_mutex_unlock(&s->mutex);return rc ? -1 : 0;
}
static void *native_main(void *unused)
{ (void)unused;identity=100;native_entry(0,NULL);return NULL; }
SceUID sceKernelCreateThread(const char *name,SceKernelThreadEntry entry,int priority,SceSize stack,SceUInt attr,int affinity,const SceKernelThreadOptParam *opt)
{
    (void)name;(void)opt;assert(priority==0x10000120 && stack==64*1024 && !attr && affinity==SCE_KERNEL_CPU_MASK_USER_ALL);
    native_entry=entry;return fail_init==4 ? -34 : 100;
}
int sceKernelStartThread(SceUID uid,SceSize args,void *argp)
{ assert(uid==100 && !args && !argp);if(fail_init==5)return -35;native_started=1;return pthread_create(&native_thread,NULL,native_main,NULL); }
int sceKernelWaitThreadEnd(SceUID uid,int *status,unsigned *timeout)
{ (void)timeout;assert(uid==100 && !status && native_started);if(join_timeouts) {join_timeouts--;return -1;}int rc=pthread_join(native_thread,NULL);native_started=0;return rc; }
int sceKernelDeleteThread(SceUID uid)
{ assert(uid==100 && !native_started);atomic_fetch_add(&deleted_threads,1);return 0; }
int sceIoMkdir(const char *p,SceMode mode) { (void)p;(void)mode;return 0; }
int sceIoGetstat(const char *p,SceIoStat *s) { (void)p;(void)s;return -1; }
int sceIoRename(const char *a,const char *b) { (void)a;(void)b;return 0; }
int sceIoRemove(const char *p) { (void)p;return 0; }
SceUID sceIoOpen(const char *p,int flags,SceMode mode)
{ (void)p;(void)flags;(void)mode;atomic_fetch_add(&opens,1);return fail_init==6 ? -36 : 10; }
SceSSize sceIoWrite(SceUID fd,const void *text,SceSize size)
{
    assert(fd==10);pthread_mutex_lock(&io);
    atomic_fetch_add(&file_calls,1);
    if(identity!=100 && ordinary_block) {
        atomic_fetch_add(&ordinary_entered,1);pthread_cond_broadcast(&changed);
        while(!ordinary_permits)pthread_cond_wait(&changed,&io);
        ordinary_permits--;
    }
    if(identity==100) {
        atomic_fetch_add(&writer_file_calls,1);
        atomic_fetch_add(&writer_entered,1);pthread_cond_broadcast(&changed);
        while(write_block && !write_permits)pthread_cond_wait(&changed,&io);
        if(write_block)write_permits--;
        if(self_check) { self_check=0;pthread_mutex_unlock(&io);
            assert(xv_log_flush_wait(1000)==XV_LOG_SELF);assert(xv_log_shutdown(1000)==XV_LOG_SELF);
            assert(xv_log_async_init()==XV_LOG_SELF);assert(xv_log_async_set_enabled(0,1000)==XV_LOG_SELF);
            atomic_store(&self_checked,1);pthread_mutex_lock(&io); }
    }
    if(fail_after>=0 && output_n>=(unsigned)fail_after) {
        int rc=write_error;pthread_mutex_unlock(&io);return rc==99 ? (SceSSize)size+1 : rc;
    }
    if(short_write && size>(unsigned)short_write)size=(unsigned)short_write;
    if(fail_after>=0 && size>(unsigned)fail_after-output_n)size=(unsigned)fail_after-output_n;
    assert(size<=CAP-output_n);memcpy(output+output_n,text,size);output_n+=size;
    pthread_mutex_unlock(&io);return size;
}
int sceIoSyncByFd(SceUID fd,int flag)
{ assert(fd==10 && !flag);pthread_mutex_lock(&io);atomic_fetch_add(&sync_calls,1);int rc=sync_error;pthread_mutex_unlock(&io);return rc; }
int sceClibPrintf(const char *fmt,...)
{
    char b[1024];va_list ap;va_start(ap,fmt);int n=vsnprintf(b,sizeof b,fmt,ap);va_end(ap);
    assert(n>=0 && n<(int)sizeof b);pthread_mutex_lock(&io);
    int diagnostic=!strncmp(b,"[log-worker]",12);
    if(identity==100 && !diagnostic) {
        atomic_fetch_add(&console_entered,1);pthread_cond_broadcast(&changed);
        while(console_block && !console_permits)pthread_cond_wait(&changed,&io);
        if(console_block)console_permits--;
        if(console_fail_after>=0) {
            if(console_n>=(unsigned)console_fail_after) { int rc=console_error;pthread_mutex_unlock(&io);return rc; }
        }
    }
    assert((unsigned)n<CAP-console_n);memcpy(console_output+console_n,b,n);console_n+=n;
    int rc=console_status>=0 ? console_status : n;
    pthread_mutex_unlock(&io);return rc;
}

static xv_log_status status(void) { xv_log_status s;xv_log_get_status(&s);return s; }
static void wait_value(atomic_int *value,int minimum)
{ uint64_t t=sceKernelGetProcessTimeWide();while(atomic_load(value)<minimum) { assert(sceKernelGetProcessTimeWide()-t<2000000);sceKernelDelayThread(100); } }
static void wait_error(void)
{ uint64_t t=sceKernelGetProcessTimeWide();while(status().state!=XV_LOG_ERROR) { assert(sceKernelGetProcessTimeWide()-t<2000000);sceKernelDelayThread(100); } }
static void start(void) { setenv("XV_PROFILE_ASYNC_REPORT","1",1);assert(xv_log_async_start()==XV_LOG_OK); }
static void report(const char *text,unsigned length,unsigned frame)
{ assert(xv_log_report_begin_frame(frame));assert(!xv_log_report_begin());xv_log_write(text,length);xv_log_report_end(); }
static void release_writes(unsigned count)
{ pthread_mutex_lock(&io);write_permits+=count;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&io); }
static void verify(const char *text,size_t n)
{ pthread_mutex_lock(&io);assert(output_n==n && !memcmp(output,text,n));pthread_mutex_unlock(&io); }
static atomic_int producer_done,flush_done;
static int flush_result;
static void *producer(void *unused)
{ (void)unused;identity=2;report("E",1,55);atomic_store(&producer_done,1);return NULL; }
static void *flusher(void *unused)
{ (void)unused;identity=3;flush_result=xv_log_flush_wait(1000000);atomic_store(&flush_done,1);return NULL; }
typedef struct { SceUID id; int result; atomic_int done; } flush_call;
static void *concurrent_flusher(void *arg)
{
    flush_call *call=arg;identity=call->id;
    call->result=xv_log_flush_wait(1000000);atomic_store(&call->done,1);return NULL;
}
static void *critical(void *unused)
{ (void)unused;identity=4;assert(!xv_log_report_begin());xv_log_report_end();xv_log_criticalf("CRIT");return NULL; }
static void *cold_log(void *id)
{ identity=10+(int)(uintptr_t)id;xv_log_criticalf("cold %d\n",identity);return NULL; }
static void *foreign_control(void *unused)
{
    (void)unused;identity=6;assert(xv_log_async_available());
    assert(xv_log_async_init()==XV_LOG_BUSY);
    assert(xv_log_async_set_enabled(!xv_log_async_enabled(),100000)==XV_LOG_BUSY);return NULL;
}
static void *claim_startup(void *unused)
{
    (void)unused;identity=6;int before=atomic_load(&sync_calls);
    assert(xv_log_async_init()==XV_LOG_OK && xv_log_async_enabled());
    assert(xv_log_async_init()==XV_LOG_OK && xv_log_async_enabled());
    assert(atomic_load(&sync_calls)==before && status().accepted==1);
    assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK);
    assert(xv_log_async_init()==XV_LOG_OK && !xv_log_async_enabled());return NULL;
}
static atomic_int foreign_open,foreign_end;
static void *foreign_report(void *unused)
{
    (void)unused;identity=6;assert(xv_log_report_begin_frame(7));xv_log_write("F",1);
    atomic_store(&foreign_open,1);while(!atomic_load(&foreign_end))sceKernelDelayThread(100);
    xv_log_report_end();return NULL;
}
static void *ordinary_log(void *unused)
{ (void)unused;identity=4;xv_log_criticalf("CRIT");return NULL; }
static void release_ordinary(void)
{ pthread_mutex_lock(&io);ordinary_permits=10;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&io); }
static void *transition_observer(void *unused)
{
    (void)unused;identity=6;
    uint64_t start=log_now();while(!status().transition) {assert(log_now()-start<2000000);sceKernelDelayThread(100);}
    assert(!xv_log_async_enabled());
    assert(!xv_log_report_begin_frame(99) && !xv_log_report_begin_async_frame(99));
    assert(!status().open_report);
    assert(xv_log_async_init()==XV_LOG_BUSY && xv_log_async_set_enabled(1,1000)==XV_LOG_BUSY);
    assert(xv_log_shutdown(1000)==XV_LOG_BUSY && xv_log_flush_wait(1000)==XV_LOG_BUSY);
    release_ordinary();return NULL;
}

#ifdef TEST_EXIT_PROTOCOL
#include "../../runtime/xv_update.h"
#define XV_RUN_RECOMP 1
#define XV_LOG xv_logf
#define SCE_KERNEL_POWER_TICK_DEFAULT 0
#define SCE_DISPLAY_SETBUF_NEXTFRAME 1
typedef struct { void *ctx; } xv_gfx_t;
static xv_gfx_t g_gfx={(void *)1};
static atomic_int remote_live=1,exit_done,exit_stage;
unsigned xv_update_requested(void) { return 1; }
void xv_update_progress(unsigned stage) { assert(atomic_load(&remote_live));atomic_store(&exit_stage,stage); }
int scePowerRequestDisplayOn(void) { return 0; }
int sceKernelPowerTick(SceKernelPowerTickType type) { assert(!type);return 0; }
void sceGxmFinish(void *ctx) { assert(ctx==g_gfx.ctx && atomic_load(&remote_live)); }
void sceGxmDisplayQueueFinish(void) { assert(atomic_load(&remote_live)); }
int sceDisplaySetFrameBuf(void *frame,unsigned mode) { assert(!frame && mode==1);return 0; }
int sceDisplayWaitVblankStart(void) { return 0; }
void xv_remote_stop(void) { xv_log_status s=status();assert(s.state==XV_LOG_STOPPED && s.accepted==s.synced);atomic_store(&remote_live,0); }
void xv_net_shutdown(void) { assert(!atomic_load(&remote_live)); }
#include "log_exit.inc"
static void *exit_thread(void *unused)
{ (void)unused;identity=5;xv_finish_for_exit();atomic_store(&exit_done,1);return NULL; }
#endif

static void toggle_test(const char *test)
{
    assert(xv_log_async_available() && !xv_log_async_enabled() && !atomic_load(&creates));
    assert(xv_log_async_set_enabled(1,1000)==XV_LOG_UNAVAILABLE);
    if(!strcmp(test,"toggle-startup")) {
        start();report("A",1,1);pthread_t owner;assert(!pthread_create(&owner,NULL,claim_startup,NULL));
        assert(!pthread_join(owner,NULL));assert(!xv_log_async_enabled());
        assert(xv_log_async_init()==XV_LOG_BUSY && xv_log_async_set_enabled(1,1000)==XV_LOG_BUSY);
        /* Startup idempotence also must not undo an explicitly selected OFF. */
        assert(xv_log_async_start()==XV_LOG_OK && !xv_log_async_enabled());
        assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("A",1);return;
    }
    unsetenv("XV_PROFILE_ASYNC_REPORT");assert(xv_log_async_init()==XV_LOG_OK);
    assert(status().state==XV_LOG_RUNNING && !status().enabled && !status().transition);
    if(!strcmp(test,"toggle-cycle")) {
        assert(xv_log_async_init()==XV_LOG_OK && atomic_load(&creates)==1);
        assert(xv_log_async_set_enabled(-1,1000)==XV_LOG_BUSY && xv_log_async_set_enabled(2,1000)==XV_LOG_BUSY);
        pthread_t foreign;assert(!pthread_create(&foreign,NULL,foreign_control,NULL));assert(!pthread_join(foreign,NULL));
        assert(xv_log_report_begin_frame(1));xv_log_write("x",1);xv_log_write("y",1);xv_log_write("z",1);
        assert(!atomic_load(&file_calls));xv_log_report_end();
        assert(atomic_load(&file_calls)==1 && !atomic_load(&writer_file_calls) && !status().accepted);
        assert(!xv_log_report_begin_async_frame(1));
        int sync=atomic_load(&sync_calls);
        assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK && atomic_load(&sync_calls)==++sync);
        assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK && atomic_load(&sync_calls)==++sync);
        assert(xv_log_async_set_enabled(1,1000000)==XV_LOG_OK && atomic_load(&sync_calls)==++sync);
        assert(xv_log_async_enabled());report("B",1,2);
        assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK && atomic_load(&sync_calls)==++sync);
        assert(!xv_log_async_enabled() && status().accepted==1 && status().synced==1);
        assert(atomic_load(&file_calls)==2 && atomic_load(&writer_file_calls)==1);
        report("C",1,3);assert(atomic_load(&file_calls)==3 && atomic_load(&writer_file_calls)==1);
        assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK && atomic_load(&sync_calls)==++sync);
        assert(!atomic_load(&deleted_threads) && native_started);verify("xyzBC",5);
    } else if(!strcmp(test,"toggle-open")) {
        assert(xv_log_report_begin_frame(1));xv_log_write("A",1);
        assert(xv_log_async_init()==XV_LOG_BUSY && xv_log_async_set_enabled(1,1000)==XV_LOG_BUSY);
        assert(!xv_log_async_enabled() && !atomic_load(&file_calls));xv_log_report_end();
        assert(xv_log_async_set_enabled(1,1000000)==XV_LOG_OK);
        assert(xv_log_report_begin_frame(2));xv_log_write("B",1);
        assert(xv_log_async_set_enabled(0,1000)==XV_LOG_BUSY && xv_log_async_enabled());
        xv_log_report_end();assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK);
        pthread_t foreign;assert(!pthread_create(&foreign,NULL,foreign_report,NULL));wait_value(&foreign_open,1);
        assert(xv_log_async_init()==XV_LOG_BUSY && xv_log_async_set_enabled(1,1000)==XV_LOG_BUSY);
        atomic_store(&foreign_end,1);assert(!pthread_join(foreign,NULL));verify("ABF",3);
    } else if(!strcmp(test,"toggle-ordinary") || !strcmp(test,"toggle-ordinary-timeout")) {
        pthread_mutex_lock(&io);ordinary_block=1;pthread_mutex_unlock(&io);
        pthread_t ordinary;assert(!pthread_create(&ordinary,NULL,ordinary_log,NULL));wait_value(&ordinary_entered,1);
        if(!strcmp(test,"toggle-ordinary")) {
            pthread_t observer;assert(!pthread_create(&observer,NULL,transition_observer,NULL));
            assert(xv_log_async_set_enabled(1,1000000)==XV_LOG_OK);assert(!pthread_join(observer,NULL));
        } else {
            assert(xv_log_async_set_enabled(1,10000)==XV_LOG_TIMEOUT);
            assert(!xv_log_async_enabled() && !status().transition && !atomic_load(&deleted_threads));
            release_ordinary();assert(xv_log_async_set_enabled(1,1000000)==XV_LOG_OK);
        }
        assert(!pthread_join(ordinary,NULL));assert(xv_log_async_enabled());
        report("A",1,1);assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK);verify("CRITA",5);
    } else if(!strcmp(test,"toggle-sync-error")) {
        pthread_mutex_lock(&io);sync_error=-99;pthread_mutex_unlock(&io);
        assert(xv_log_async_set_enabled(1,1000000)==XV_LOG_IO && !xv_log_async_enabled());
        assert(status().state==XV_LOG_ERROR && !status().transition && !atomic_load(&deleted_threads));
        assert(xv_log_async_init()==XV_LOG_IO && xv_log_async_set_enabled(1,1000)==XV_LOG_IO);
        pthread_mutex_lock(&io);sync_error=0;pthread_mutex_unlock(&io);assert(xv_log_retry()==XV_LOG_OK);
        assert(xv_log_async_set_enabled(1,1000000)==XV_LOG_OK);report("A",1,1);
        assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK);verify("A",1);
    } else {
        assert(xv_log_async_set_enabled(1,1000000)==XV_LOG_OK);
        pthread_mutex_lock(&io);
        if(!strcmp(test,"toggle-error"))fail_after=0;
        else if(!strcmp(test,"toggle-blocked-console"))console_block=1;
        else write_block=1;
        pthread_mutex_unlock(&io);report("A",1,1);
        if(!strcmp(test,"toggle-error")) {
            wait_error();assert(xv_log_async_set_enabled(0,1000)==XV_LOG_IO);
            assert(xv_log_async_enabled() && status().queued==1 && !status().transition);
            pthread_mutex_lock(&io);fail_after=-1;pthread_mutex_unlock(&io);assert(xv_log_retry()==XV_LOG_OK);
        } else {
            wait_value(!strcmp(test,"toggle-blocked-console") ? &console_entered : &writer_entered,1);
            if(!strcmp(test,"toggle-barrier")) {
                pthread_t flush;assert(!pthread_create(&flush,NULL,flusher,NULL));
                for(;;) {queue_lock();int ready=log_users!=0;queue_unlock();if(ready)break;sceKernelDelayThread(100);}
                assert(xv_log_async_set_enabled(0,1000)==XV_LOG_BUSY && xv_log_async_enabled());
                release_writes(1);wait_value(&flush_done,1);assert(!pthread_join(flush,NULL));assert(flush_result==XV_LOG_OK);
            } else {
                if(!strcmp(test,"toggle-deadline")) {deadline_reads=0;deadline_clock=1;}
                int rc=xv_log_async_set_enabled(0,10000);deadline_clock=0;
                assert(rc==XV_LOG_TIMEOUT && xv_log_async_enabled() && !status().transition);
                assert(status().queued==1 && !status().synced && !atomic_load(&deleted_threads));
                report("B",1,2);assert(status().accepted==2);
                pthread_mutex_lock(&io);console_permits=write_permits=10;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&io);
            }
        }
        assert(xv_log_async_set_enabled(0,1000000)==XV_LOG_OK);
        if(!strcmp(test,"toggle-error") || !strcmp(test,"toggle-barrier"))verify("A",1);else verify("AB",2);
    }
    assert(!status().transition && !xv_log_async_enabled());
    assert(xv_log_shutdown(1000000)==XV_LOG_OK && atomic_load(&deleted_threads)==1);
    assert(xv_log_async_init()==XV_LOG_BUSY && xv_log_async_set_enabled(1,1000)==XV_LOG_UNAVAILABLE);
}

int main(int argc,char **argv)
{
    assert(argc==2);const char *test=argv[1];
    if(!strncmp(test,"toggle-",7)) {
        toggle_test(test);
    } else if(!strncmp(test,"startup",7)) {
        fail_init=atoi(test+7);setenv("XV_PROFILE_ASYNC_REPORT","1",1);
        assert(xv_log_async_start()==XV_LOG_UNAVAILABLE);assert(!native_started);
        assert(atomic_load(&deleted_semas)==(int)sem_count);
        xv_logf("fallback\n");if(fail_init!=6)verify("fallback\n",9);
        assert(status().startup_error);assert(xv_log_flush_wait(1000)==(fail_init==6 ? XV_LOG_IO : XV_LOG_OK));
    } else if(!strcmp(test,"cold")) {
        pthread_t t[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&t[i],NULL,cold_log,(void *)(uintptr_t)i));
        for(unsigned i=0;i<8;i++)assert(!pthread_join(t[i],NULL));
        assert(atomic_load(&creates)==1 && atomic_load(&opens)==1);
        assert(output_n==64 && console_n==64 && xv_log_flush_wait(1000)==XV_LOG_OK);
    } else if(!strcmp(test,"disabled")) {
        unsetenv("XV_PROFILE_ASYNC_REPORT");assert(xv_log_async_start()==XV_LOG_UNAVAILABLE);
        setenv("XV_PROFILE_ASYNC_REPORT","0",1);assert(xv_log_async_start()==XV_LOG_UNAVAILABLE);
        setenv("XV_PROFILE_ASYNC_REPORT","1x",1);assert(xv_log_async_start()==XV_LOG_UNAVAILABLE);
        report("sync",4,1);verify("sync",4);assert(!xv_log_report_begin_async_frame(2));
        assert(xv_log_shutdown(1000)==XV_LOG_OK);assert(!native_started);
    } else if(!strncmp(test,"status-console-",15)) {
        /* Test both status and byte-count-style console success conventions
         * in the same production sink, with runtime async enabled/disabled. */
        console_status=atoi(test+15);
        int enabled=strstr(test,"-on")!=NULL;
        if(enabled)start();else {unsetenv("XV_PROFILE_ASYNC_REPORT");assert(xv_log_async_start()==XV_LOG_UNAVAILABLE);}
        char text[1024];memset(text,'C',sizeof text);
        report(text,sizeof text,1);assert(xv_log_shutdown(1000000)==XV_LOG_OK);
        verify(text,sizeof text);assert(console_n==sizeof text && !memcmp(console_output,text,sizeof text));
        xv_log_status s=status();assert(!s.error && s.synced_bytes==(enabled ? sizeof text : 0));
        assert(!native_started);
    } else if(!strcmp(test,"deadline-wrap")) {
        start();pthread_mutex_lock(&io);write_block=1;pthread_mutex_unlock(&io);
        report("A",1,1);wait_value(&writer_entered,1);
        deadline_reads=0;deadline_clock=1;
        int rc=xv_log_shutdown(10000);deadline_clock=0;
        assert(rc==XV_LOG_TIMEOUT && !atomic_load(&deleted_threads));
        assert(status().state==XV_LOG_DRAINING && status().queued==1 && !status().synced);
        release_writes(1);assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("A",1);
    } else if(!strcmp(test,"fifo")) {
        start();short_write=137;char text[65537];unsigned seed=1234;
        for(unsigned r=0;r<200;r++) {
            seed=seed*1664525+1013904223;unsigned n=r<4 ? (r==0 ? 0 : r==3 ? sizeof text : 32767+r) : seed%sizeof text;
            for(unsigned i=0;i<n;i++)text[i]=i%79 ? (char)('a'+(r+i)%26) : '\n';
            memcpy(oracle+oracle_n,text,n);oracle_n+=n;
            assert(xv_log_report_begin_frame(r));xv_log_write(text,n);memset(text,'X',n);xv_log_report_end();
        }
        assert(xv_log_flush_wait(3000000)==XV_LOG_OK);verify(oracle,oracle_n);
        assert(console_n==oracle_n && !memcmp(console_output,oracle,oracle_n));
        xv_log_status s=status();assert(s.accepted==s.written && s.written==s.synced && s.completed_report==200);
        assert(s.accepted_bytes==oracle_n && s.synced_bytes==oracle_n && s.high_water<=4);
        assert(xv_log_shutdown(1000000)==XV_LOG_OK && xv_log_shutdown(1000000)==XV_LOG_OK);
    } else if(!strcmp(test,"capacity") || !strcmp(test,"critical")) {
        start();pthread_mutex_lock(&io);write_block=1;pthread_mutex_unlock(&io);
        char first='A';report(&first,1,51);first='X';wait_value(&writer_entered,1);
        report("B",1,52);report("C",1,53);report("D",1,54);
        assert(status().queued==4 && status().accepted==4 && !status().written);
        assert(status().pending_frame==51 && status().pending_report==1 && status().pending_length==1);
        pthread_t p;assert(!pthread_create(&p,NULL,producer,NULL));
        uint64_t t=sceKernelGetProcessTimeWide();while(!status().backpressure_count) {assert(sceKernelGetProcessTimeWide()-t<2000000);sceKernelDelayThread(100);}
        assert(!atomic_load(&producer_done));assert(xv_log_flush_wait(20000)==XV_LOG_BUSY);
        pthread_t c;if(!strcmp(test,"critical")) {
            assert(!pthread_create(&c,NULL,critical,NULL));
            for(;;) {pthread_mutex_lock(&io);int visible=console_n>=5 && !memcmp(console_output+console_n-4,"CRIT",4);pthread_mutex_unlock(&io);if(visible)break;sceKernelDelayThread(100);}
        }
        assert(xv_log_shutdown(20000)==XV_LOG_TIMEOUT);assert(!atomic_load(&deleted_threads));
        release_writes(10);assert(!pthread_join(p,NULL));
        if(!strcmp(test,"critical"))assert(!pthread_join(c,NULL));
        assert(xv_log_shutdown(1000000)==XV_LOG_OK);
        if(!strcmp(test,"capacity"))verify("ABCDE",5);
        else {char periodic[5];unsigned n=0;for(size_t i=0;i<output_n;) {
                if(i+4<=output_n && !memcmp(output+i,"CRIT",4))i+=4;
                else {assert(n<5);periodic[n++]=output[i++];}
            } assert(n==5 && !memcmp(periodic,"ABCDE",5) && output_n==9);}
        assert(status().backpressure_count && status().backpressure_us);
    }
#ifdef TEST_EXIT_PROTOCOL
    else if(!strcmp(test,"update-write") || !strcmp(test,"update-console")) {
        start();pthread_mutex_lock(&io);
        if(!strcmp(test,"update-write"))write_block=1;else console_block=1;
        pthread_mutex_unlock(&io);report("periodic-head\n",14,18);
        wait_value(!strcmp(test,"update-write") ? &writer_entered : &console_entered,1);
        pthread_t e;assert(!pthread_create(&e,NULL,exit_thread,NULL));
        sceKernelDelayThread(20000);
        assert(atomic_load(&remote_live) && !atomic_load(&exit_done) && !atomic_load(&deleted_threads));
        if(!strcmp(test,"update-console"))assert(status().state==XV_LOG_DRAINING);
        pthread_mutex_lock(&io);write_permits=console_permits=20;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&io);
        assert(!pthread_join(e,NULL));assert(!atomic_load(&remote_live) && atomic_load(&exit_done));
        assert(atomic_load(&exit_stage)==XV_UPDATE_NETWORK_STOP && status().accepted==status().synced);
        assert(strstr(output,"periodic-head\n") && !strstr(strstr(output,"periodic-head\n")+14,"periodic-head\n"));
    }
#endif
    else if(!strcmp(test,"ownerflush")) {
        start();pthread_mutex_lock(&io);write_block=1;pthread_mutex_unlock(&io);
        report("A",1,1);wait_value(&writer_entered,1);report("B",1,2);report("C",1,3);report("D",1,4);
        assert(xv_log_report_begin_frame(5));xv_log_write("E",1);
        assert(xv_log_shutdown(10000)==XV_LOG_BUSY && status().state==XV_LOG_RUNNING);
        uint64_t before=sceKernelGetProcessTimeWide();assert(xv_log_flush_wait(10000)==XV_LOG_TIMEOUT);
        assert(sceKernelGetProcessTimeWide()-before<100000 && g_report_used==1 && status().accepted==4);
        release_writes(10);xv_log_report_end();assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("ABCDE",5);
    } else if(!strcmp(test,"errorfull")) {
        start();pthread_mutex_lock(&io);fail_after=0;pthread_mutex_unlock(&io);
        report("A",1,1);wait_error();report("B",1,2);report("C",1,3);report("D",1,4);
        pthread_t p;assert(!pthread_create(&p,NULL,producer,NULL));
        while(!status().backpressure_count)sceKernelDelayThread(100);
        assert(xv_log_shutdown(10000)==XV_LOG_IO && !atomic_load(&producer_done));
        pthread_mutex_lock(&io);fail_after=-1;pthread_mutex_unlock(&io);assert(xv_log_retry()==XV_LOG_OK);
        assert(!pthread_join(p,NULL));assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("ABCDE",5);
    } else if(!strcmp(test,"barrier")) {
        start();pthread_mutex_lock(&io);write_block=1;pthread_mutex_unlock(&io);
        report("A",1,1);wait_value(&writer_entered,1);
        pthread_t f;assert(!pthread_create(&f,NULL,flusher,NULL));
        for(;;) {queue_lock();int pending=log_sync_request>log_sync_done;queue_unlock();if(pending)break;sceKernelDelayThread(100);}
        report("B",1,2);release_writes(1);wait_value(&flush_done,1);assert(!pthread_join(f,NULL));
        assert(flush_result==XV_LOG_OK && status().synced==1 && status().accepted==2);
        wait_value(&writer_entered,2);assert(status().written==1);release_writes(1);
        assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("AB",2);
    } else if(!strcmp(test,"simultaneous-flushers")) {
        start();pthread_mutex_lock(&io);write_block=1;pthread_mutex_unlock(&io);
        report("A",1,1);wait_value(&writer_entered,1);
        flush_call first={.id=3},second={.id=6};pthread_t a,b;
        assert(!pthread_create(&a,NULL,concurrent_flusher,&first));
        for(;;) {queue_lock();int ready=log_users==1 && log_sync_request>log_sync_done && log_sync_target==1;queue_unlock();if(ready)break;sceKernelDelayThread(100);}
        report("B",1,2);assert(!pthread_create(&b,NULL,concurrent_flusher,&second));
        for(;;) {queue_lock();int ready=log_users==2;assert(log_sync_target==1);queue_unlock();if(ready)break;sceKernelDelayThread(100);}
        assert(!atomic_load(&first.done) && !atomic_load(&second.done));
        release_writes(1);wait_value(&first.done,1);assert(!pthread_join(a,NULL));
        wait_value(&writer_entered,2);
        assert(first.result==XV_LOG_OK && !atomic_load(&second.done));
        assert(status().written==1 && status().synced==1 && status().accepted==2);
        release_writes(1);wait_value(&second.done,1);assert(!pthread_join(b,NULL));
        assert(second.result==XV_LOG_OK && status().synced==2);
        assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("AB",2);
    } else if(!strcmp(test,"openflush")) {
        start();assert(xv_log_report_begin_frame(7));xv_log_write("A",1);
        assert(xv_log_flush_wait(1000000)==XV_LOG_OK);assert(status().synced==1 && !status().completed_report);
        xv_log_write("B",1);xv_log_report_end();assert(xv_log_shutdown(1000000)==XV_LOG_OK);
        assert(status().completed_report==1 && status().synced==2);verify("AB",2);
    } else if(!strcmp(test,"join")) {
        start();report("join",4,1);join_timeouts=1;
        assert(xv_log_shutdown(1000000)==XV_LOG_TIMEOUT && !atomic_load(&deleted_threads));
        assert(xv_log_shutdown(1000000)==XV_LOG_OK && atomic_load(&deleted_threads)==1);verify("join",4);
    } else if(!strcmp(test,"hints")) {
        start();atomic_store(&failed_hints,1);report("hints",5,1);
        assert(xv_log_flush_wait(1000000)==XV_LOG_OK);assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("hints",5);
    } else if(!strcmp(test,"ownercritical")) {
        start();assert(xv_log_report_begin_frame(2));xv_log_write("periodic",8);xv_log_criticalf("urgent");
        verify("urgent",6);xv_log_report_end();assert(xv_log_shutdown(1000000)==XV_LOG_OK);
        verify("urgentperiodic",14);assert(status().accepted_bytes==8);
    } else if(!strcmp(test,"self")) {
        start();pthread_mutex_lock(&io);self_check=1;pthread_mutex_unlock(&io);
        report("self",4,1);assert(xv_log_flush_wait(1000000)==XV_LOG_OK);assert(atomic_load(&self_checked));
        assert(xv_log_shutdown(1000000)==XV_LOG_OK);
    } else if(!strcmp(test,"console")) {
        start();pthread_mutex_lock(&io);console_block=1;pthread_mutex_unlock(&io);
        report("console",7,9);wait_value(&console_entered,1);assert(status().accepted==1 && !status().written);
        assert(xv_log_flush_wait(10000)==XV_LOG_TIMEOUT);
        pthread_mutex_lock(&io);console_permits=10;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&io);
        assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("console",7);
    } else if(!strcmp(test,"syncerror")) {
        start();pthread_mutex_lock(&io);sync_error=-99;pthread_mutex_unlock(&io);
        report("sync",4,1);assert(xv_log_flush_wait(1000000)==XV_LOG_IO);
        assert(status().written==1 && !status().synced && status().error);
        pthread_mutex_lock(&io);sync_error=0;pthread_mutex_unlock(&io);assert(xv_log_retry()==XV_LOG_OK);
        assert(xv_log_shutdown(1000000)==XV_LOG_OK);verify("sync",4);assert(status().synced==1);
    } else {
        start();pthread_mutex_lock(&io);
        if(!strcmp(test,"consoleerror"))console_fail_after=5;
        else {fail_after=!strcmp(test,"partial") ? 5 : 0;write_error=!strcmp(test,"zero") ? 0 : !strcmp(test,"impossible") ? 99 : -77;short_write=3;}
        /* Console failure occurs after a whole accepted newline-terminated
         * chunk. Its return value cannot describe a partial console write. */
        const char *text=!strcmp(test,"consoleerror") ? "abcd\nfghijkX" : "abcdefghijk\n";
        pthread_mutex_unlock(&io);report(text,12,42);wait_error();
        xv_log_status s=status();assert(s.accepted==1 && !s.written && !s.synced && !s.completed_report);
        assert(s.failed_sequence==1 && s.pending_frame==42 && s.queued==1);
        if(!strcmp(test,"partial"))assert(s.failed_file_offset==5 && s.written_bytes==5);
        if(!strcmp(test,"consoleerror"))assert(s.failed_console_offset==5 && !s.written_bytes);
        assert(xv_log_flush_wait(10000)==XV_LOG_IO && xv_log_shutdown(10000)==XV_LOG_IO);
        assert(!atomic_load(&deleted_threads));pthread_mutex_lock(&io);fail_after=console_fail_after=-1;pthread_mutex_unlock(&io);
        assert(xv_log_retry()==XV_LOG_OK);assert(xv_log_shutdown(1000000)==XV_LOG_OK);
        verify(text,12);s=status();assert(s.written==1 && s.synced==1 && s.completed_report==1 && s.written_bytes==12);
    }
    printf("PASS: %s\n",test);return 0;
}
