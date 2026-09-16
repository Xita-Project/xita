/* Two diagnostic filesystem polls, not controller sampling or screenshots.
 * Timing is off except during a bounded comparison. All state is guest-owned. */
#include "xk_diag_poll.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef __vita__
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#endif
uint64_t xk_os_monotonic_us(void);
void xk_os_log(const char *, ...);
void xd3d_hist_arm(void) __attribute__((weak));
unsigned xd3d_frame(void) __attribute__((weak));
static int shot_config=-1, shot_last=-1;
static unsigned shot_tick, hist_tick, selected, suppressed, measuring, contaminated;
typedef struct {
    uint64_t polls, due, skipped, runs, us, max_us, entries, errors, triggers;
    uint64_t frame_us;
    unsigned frame_runs, max_frame;
} poll_stats;
static poll_stats stats[2];
enum { EVENTS=512 };
static struct { uint64_t interval, shot, hist; unsigned frame, shot_runs, hist_runs; } events[EVENTS];
static unsigned event_count, overflow, frame_count;
static int shot_enabled(void)
{
    if(shot_config<0) {const char *e=getenv("XV_SHOT_DUMP");shot_config=e?atoi(e)!=0:0;}
    return shot_config;
}
int xv_diag_poll_available(unsigned path)
{
    if(selected)return 0;
    if(path==XV_DIAG_HIST)return 1;
#ifdef __vita__
    return path==XV_DIAG_SHOT && shot_enabled();
#else
    (void)path;return 0;
#endif
}
void xv_diag_poll_override(unsigned path,int suppress)
{
    /* No native I/O yields the guest baton, so a boundary cannot interrupt a
     * scan. Keep tick phase advancing while suppressed; never replay old ticks.
     * Rebase screenshot count on the next COMPLETE scan after suppression.
     * Photos taken meanwhile remain intact, without a stale diagnostic trace. */
    if(path!=XV_DIAG_SHOT && path!=XV_DIAG_HIST)abort();
    if(suppress < -1 || suppress > 1 || (selected && selected!=path))abort();
    if(!selected)contaminated=0;
    if(selected==XV_DIAG_SHOT && suppressed && suppress!=1)shot_last=-1;
    measuring=0;
    if(suppress<0) {selected=suppressed=0;return;}
    selected=path;suppressed=!!suppress;
}
void xv_diag_poll_measure(void)
{
    memset(stats,0,sizeof stats);event_count=overflow=frame_count=0;
    measuring=selected!=0;
}
static uint64_t begin(unsigned path)
{
    if(!measuring)return 0;
    stats[path-1].runs++;return xk_os_monotonic_us()+1;
}
static void end(unsigned path,uint64_t token,unsigned entries,unsigned errors,unsigned trigger)
{
    if(selected && trigger)contaminated=1; /* includes settling intervals */
    if(!token)return;
    poll_stats *s=&stats[path-1];uint64_t us=xk_os_monotonic_us()-(token-1);
    s->us+=us;s->frame_us+=us;s->frame_runs++;s->entries+=entries;s->errors+=errors;s->triggers+=trigger;
    if(us>s->max_us) {s->max_us=us;s->max_frame=xd3d_frame?xd3d_frame():0;}
}
static int allowed(unsigned path)
{
    int skip=selected==path && suppressed;
    if(measuring) {stats[path-1].due++;stats[path-1].skipped+=skip;}
    return !skip;
}
void xv_diag_poll_shot(void)
{
#ifdef __vita__
    if(!shot_enabled())return;
    if(measuring)stats[0].polls++;
    if(++shot_tick%180u || !allowed(XV_DIAG_SHOT))return;
    uint64_t token=begin(XV_DIAG_SHOT);
    unsigned entries=0,errors=0,trigger=0;
    int count=0,rc;
    SceUID d=sceIoDopen("ux0:picture/SCREENSHOT");
    if(d<0) {end(XV_DIAG_SHOT,token,0,1,0);return;}
    SceIoDirent e;
    while((rc=sceIoDread(d,(memset(&e,0,sizeof e),&e)))>0) {
        entries++;
        if(!SCE_S_ISDIR(e.d_stat.st_mode)) {count++;continue;}
        char sub[128];int n=snprintf(sub,sizeof sub,"ux0:picture/SCREENSHOT/%s",e.d_name);
        if(n<0 || (unsigned)n>=sizeof sub) {errors++;continue;}
        SceUID d2=sceIoDopen(sub);
        if(d2<0) {errors++;continue;}
        SceIoDirent e2;int rc2;
        while((rc2=sceIoDread(d2,(memset(&e2,0,sizeof e2),&e2)))>0) {
            entries++;if(!SCE_S_ISDIR(e2.d_stat.st_mode))count++;
        }
        if(rc2<0)errors++;
        if(sceIoDclose(d2)<0)errors++;
    }
    if(rc<0)errors++;
    if(sceIoDclose(d)<0)errors++;
    /* Partial directory walks cannot establish a smaller baseline and cause a
     * false screenshot on the next successful walk. Retain the last good one. */
    if(!errors) {
        trigger=shot_last>=0 && count>shot_last;
        shot_last=count;
    }
    end(XV_DIAG_SHOT,token,entries,errors,trigger);
    if(trigger && xd3d_hist_arm)xd3d_hist_arm();
#endif
}
int xv_diag_poll_hist(void)
{
    if(measuring)stats[1].polls++;
    if((++hist_tick&15u) || !allowed(XV_DIAG_HIST))return 0;
    uint64_t token=begin(XV_DIAG_HIST);unsigned errors=0,trigger=0;
    errno=0;FILE *f=fopen("ux0:data/xita/hist.now","rb");
    if(f) {
        /* Arm only after consuming the request. A failed close/remove leaves
         * it retryable and must not repeatedly trace the same request. */
        if(fclose(f))errors++;
        else if(remove("ux0:data/xita/hist.now"))errors++;
        else trigger=1;
    } else if(errno!=ENOENT)errors++;
    end(XV_DIAG_HIST,token,0,errors,trigger);
    return trigger;
}
void xv_diag_poll_frame(uint64_t elapsed_us)
{
    if(!measuring)return;
    frame_count++;
    if(stats[0].frame_runs || stats[1].frame_runs) {
        if(event_count<EVENTS) {
            events[event_count].interval=elapsed_us;events[event_count].frame=frame_count;
            events[event_count].shot=stats[0].frame_us;events[event_count].hist=stats[1].frame_us;
            events[event_count].shot_runs=stats[0].frame_runs;events[event_count].hist_runs=stats[1].frame_runs;
            event_count++;
        } else overflow++;
    }
    for(unsigned i=0;i<2;i++) {stats[i].frame_us=0;stats[i].frame_runs=0;}
}
int xv_diag_poll_report(unsigned phase)
{
    measuring=0;int valid=!overflow && !contaminated && frame_count==1800 && selected && stats[selected-1].due>0;
    for(unsigned i=0;i<2;i++) {
        poll_stats *s=&stats[i];
        if(s->triggers || s->errors)valid=0;
        xk_os_log("[diagnostic-poll] phase %u path %s polls %llu due %llu skipped %llu runs %llu us %llu max-us %llu max-frame %u entries %llu errors %llu triggers %llu\n",
            phase,i?"hist":"shot",(unsigned long long)s->polls,(unsigned long long)s->due,
            (unsigned long long)s->skipped,(unsigned long long)s->runs,(unsigned long long)s->us,
            (unsigned long long)s->max_us,s->max_frame,(unsigned long long)s->entries,
            (unsigned long long)s->errors,(unsigned long long)s->triggers);
    }
    for(unsigned i=0;i<event_count;i++)
        xk_os_log("[diagnostic-poll-frame] phase %u interval %u elapsed-us %llu shot-us %llu hist-us %llu runs %u/%u\n",phase,events[i].frame,
            (unsigned long long)events[i].interval,(unsigned long long)events[i].shot,
            (unsigned long long)events[i].hist,events[i].shot_runs,events[i].hist_runs);
    xk_os_log("[diagnostic-poll] phase %u frames %u event-overflow %u valid %d; interval 1 includes phase marker; elapsed includes scheduling, trace output excluded\n",phase,frame_count,overflow,valid);
    return valid;
}
