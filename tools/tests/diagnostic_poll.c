/* Production polling I/O plus real benchmark transitions. No Vita/emulator. */
#define __vita__ 1
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include "../../runtime/xv_benchmark.c"
static FILE *probe_open(const char *,const char *);
static int probe_close(FILE *),probe_remove(const char *);
#define fopen probe_open
#define fclose probe_close
#define remove probe_remove
#include "../../recomp/kernel/xk_diag_poll.c"
#undef fopen
#undef fclose
#undef remove
static unsigned photos=2, arms, root_pos, sub_pos, opened, closed, probes, removes, clocks;
static int fault, pending_file, close_error, remove_error, open_error;
static unsigned game_frame, drains;
static uint64_t now_us;
static char output[300000];static unsigned output_size;
static const float camera[6]={1,2,3,1,0,0};
uint64_t xk_os_monotonic_us(void) {clocks++;return now_us+=10;}
unsigned xd3d_frame(void) {return game_frame;}
void xd3d_hist_arm(void) {arms++;}
static void log_output(const char *fmt,va_list a)
{int n=vsnprintf(output+output_size,sizeof output-output_size,fmt,a);assert(n>=0&&(unsigned)n<sizeof output-output_size);output_size+=(unsigned)n;}
void xv_logf(const char *fmt,...) {va_list a;va_start(a,fmt);log_output(fmt,a);va_end(a);}
void xk_os_log(const char *fmt,...) {va_list a;va_start(a,fmt);log_output(fmt,a);va_end(a);}
void xv_benchmark_optimizations(int enabled) {(void)enabled;assert(xv_benchmark_compare_diagnostic_poll());drains++;}
SceUID sceIoDopen(const char *path)
{
    if(!strcmp(path,"ux0:picture/SCREENSHOT")) {if(fault==1)return -1;root_pos=0;opened++;return 10;}
    assert(!strcmp(path,"ux0:picture/SCREENSHOT/game"));
    if(fault==2)return -1;
    sub_pos=0;opened++;return 11;
}
int sceIoDread(SceUID fd,SceIoDirent *e)
{
    assert(fd==10||fd==11);
    if(fd==10) {
        if(root_pos++)return fault==3?-1:0;
        strcpy(e->d_name,"game");e->d_stat.st_mode=SCE_S_IFDIR;return 1;
    }
    if(fault==4 && sub_pos==1)return -1;
    if(sub_pos++>=photos)return 0;
    e->d_stat.st_mode=SCE_S_IFREG;return 1;
}
int sceIoDclose(SceUID fd) {assert(fd==10||fd==11);closed++;return fault==5?-1:0;}
static FILE *probe_open(const char *path,const char *mode)
{assert(!strcmp(path,"ux0:data/xita/hist.now")&&!strcmp(mode,"rb"));probes++;if(open_error){errno=EACCES;return NULL;}if(!pending_file){errno=ENOENT;return NULL;}return (FILE *)&pending_file;}
static int probe_close(FILE *f) {assert(f==(FILE *)&pending_file);return close_error?-1:0;}
static int probe_remove(const char *path)
{assert(!strcmp(path,"ux0:data/xita/hist.now"));removes++;if(remove_error)return -1;pending_file=0;return 0;}
static void reset(void)
{
    if(selected)xv_diag_poll_override(selected,-1);
    shot_config=-1;shot_last=-1;shot_tick=hist_tick=0;
    memset(stats,0,sizeof stats);memset(&b,0,sizeof b);status=request_state=remote_ready=remote_kind=0;
    event_count=overflow=frame_count=0;photos=2;arms=opened=closed=probes=removes=clocks=game_frame=drains=0;
    fault=pending_file=close_error=remove_error=open_error=0;now_us=1;output_size=0;output[0]=0;
    setenv("XV_SHOT_DUMP","1",1);
}
static void scan(void) {for(unsigned i=0;i<180;i++)xv_diag_poll_shot();}
static int probe(void) {int hit=0;for(unsigned i=0;i<16;i++)hit+=xv_diag_poll_hist();return hit;}
static void tick(void)
{
    game_frame++;
    for(unsigned i=0;i<3;i++)xv_diag_poll_shot();
    (void)xv_diag_poll_hist();
    now_us+=100000;
    unsigned h=xv_benchmark_step(now_us,544,1,camera);if(h)xv_benchmark_applied(now_us,h);
}
static void start(unsigned kind)
{
    xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(kind));
    assert(!selected&&!clocks&&!drains);xv_benchmark_remote_poll(1);tick();assert(b.active);
}
int main(void)
{
    reset();unsetenv("XV_SHOT_DUMP");assert(!xv_diag_poll_available(XV_DIAG_SHOT));scan();assert(!opened&&!clocks);
    reset();for(unsigned i=0;i<179;i++)xv_diag_poll_shot();assert(!opened);xv_diag_poll_shot();assert(opened==closed&&opened==2&&!arms&&!clocks);
    photos++;scan();assert(arms==1);assert(probe()==0&&!clocks);
    pending_file=1;assert(probe()==1&&!pending_file&&removes==1);assert(!probe());
    /* Partial scans cannot lower or replace the baseline, and every opened
     * descriptor closes even on nested errors. Fresh photos still trigger. */
    for(int f=1;f<=5;f++) {
        reset();scan();photos=1;fault=f;scan();assert(shot_last==2&&opened==closed&&!arms);
        fault=0;photos=2;scan();assert(!arms);photos=3;scan();assert(arms==1);
    }
    /* Independent triggers: suppressing one poll leaves the other live. */
    reset();scan();unsigned phase=shot_tick;xv_diag_poll_override(XV_DIAG_SHOT,1);
    photos=90;scan();assert(shot_tick==phase+180&&shot_last==2);pending_file=1;assert(probe()==1);
    xv_diag_poll_override(XV_DIAG_SHOT,0);assert(shot_last==-1);fault=4;scan();assert(shot_last==-1);
    fault=0;scan();assert(shot_last==90&&!arms);photos++;scan();assert(arms==1);
    xv_diag_poll_override(XV_DIAG_SHOT,-1);assert(!selected&&!suppressed&&shot_tick==phase+720);
    reset();scan();xv_diag_poll_override(XV_DIAG_HIST,1);pending_file=1;assert(!probe()&&pending_file&&!probes);
    photos++;scan();assert(arms==1);xv_diag_poll_override(XV_DIAG_HIST,0);
    remove_error=1;assert(!probe()&&pending_file);remove_error=0;close_error=1;assert(!probe()&&pending_file);
    close_error=0;assert(probe()==1&&!pending_file);assert(!probe());
    /* Exact measurement, errors/trace triggers invalidate, no polling clock
     * reads outside capture. Frame attribution survives pacing sort. */
    reset();xv_diag_poll_override(XV_DIAG_HIST,0);xv_diag_poll_measure();
    for(unsigned i=0;i<1800;i++) {game_frame++;for(unsigned p=0;p<3;p++)xv_diag_poll_shot();(void)xv_diag_poll_hist();xv_diag_poll_frame(100000);}
    assert(stats[0].polls==5400&&stats[0].runs==30&&stats[1].polls==1800&&stats[1].runs==112);
    assert(clocks==2*(30+112)&&stats[0].us==300&&stats[1].us==1120&&xv_diag_poll_report(1));
    assert(strstr(output,"frames 1800 event-overflow 0 valid 1"));
    reset();xv_diag_poll_override(XV_DIAG_HIST,0);xv_diag_poll_measure();
    for(unsigned i=0;i<1800;i++) {open_error=i<16;(void)xv_diag_poll_hist();xv_diag_poll_frame(100000);}
    assert(stats[1].errors==1&&!xv_diag_poll_report(1));
    reset();xv_diag_poll_override(XV_DIAG_HIST,0);xv_diag_poll_measure();pending_file=1;
    for(unsigned i=0;i<1800;i++) {(void)xv_diag_poll_hist();xv_diag_poll_frame(100000);}
    assert(stats[1].triggers==1&&!xv_diag_poll_report(1));
    reset();xv_diag_poll_override(XV_DIAG_HIST,0);pending_file=1;assert(probe());xv_diag_poll_measure();
    for(unsigned i=0;i<1800;i++) {(void)xv_diag_poll_hist();xv_diag_poll_frame(100000);}
    assert(!stats[1].triggers&&!xv_diag_poll_report(1)); /* settling trace remains invalid */
    /* Full actual benchmark, suppression only in middle arm, both selectors,
     * cancellation, lost view, disabled config and reserved-number rejection. */
    for(unsigned k=0;k<2;k++) {
        reset();start(k?XV_BENCH_DIAGNOSTIC_HIST:XV_BENCH_DIAGNOSTIC_POLL);
        unsigned guard=0;while(b.active) {tick();assert(++guard<6000);}
        assert(!selected&&!suppressed&&!xv_benchmark_remote_busy()&&drains==4);
        assert(strstr(output,"baseline/suppressed/baseline")&&strstr(output,"fps comparable-view 1")&&strstr(output,"restored 544p"));
        for(unsigned p=1;p<=3;p++) {char token[80];snprintf(token,sizeof token,"[diagnostic-poll] phase %u frames 1800 event-overflow 0 valid 1",p);assert(strstr(output,token));}
        for(unsigned cancel=0;cancel<2;cancel++) {
            reset();start(k?XV_BENCH_DIAGNOSTIC_HIST:XV_BENCH_DIAGNOSTIC_POLL);
            while(b.phase==0)tick();
            assert(suppressed);
            if(cancel)xv_benchmark_compare_toggle();
            unsigned h=xv_benchmark_step(now_us+=100000,544,cancel,camera);assert(h==544);xv_benchmark_applied(now_us,h);
            assert(!selected&&!suppressed&&!b.active&&!xv_benchmark_remote_busy());
        }
    }
    reset();unsetenv("XV_SHOT_DUMP");xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_DIAGNOSTIC_POLL));xv_benchmark_remote_poll(1);tick();assert(!b.active&&!selected&&!drains);
    assert(xv_benchmark_remote_request(36)<0&&xv_benchmark_remote_request(37)<0&&xv_benchmark_remote_request(38|512)<0);
    puts("PASS: real diagnostic polls, independent triggers/failures, bounded counters and benchmark completion/cancel/restore");
    return 0;
}
