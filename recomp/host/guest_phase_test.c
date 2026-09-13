#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../xv_phase.h"
static uint64_t now;
static unsigned clock_reads, log_count;
static char logs[16384];
uint64_t xk_os_monotonic_us(void) { clock_reads++; return now; }
void xk_os_log(const char *fmt, ...)
{
    va_list ap; va_start(ap,fmt);
    vsnprintf(logs+log_count,sizeof logs-log_count,fmt,ap); va_end(ap);
    log_count=(unsigned)strlen(logs);
}
#include "../xv_phase.c"
const xv_phase_target xv_phase_targets[] = {{1,"root"},{2,"child"},{3,"other"},{4,"recursive"}};
const unsigned xv_phase_target_count = 4;

static int contexts[40];
static void reset(int enabled)
{
    memset(owners,0,sizeof owners);memset(stats,0,sizeof stats);
    frames=dropped=invalid=clock_reads=log_count=0;now=1;logs[0]=0;
    assert(setenv("XV_PHASE_TIMING",enabled?"1":"0",1)==0);
    xv_phase_init();
}
static void report(void) { for(unsigned i=0;i<60;i++)xv_phase_frame(i+1); }
static int cleanup_return(int early)
{
    XV_PHASE_SCOPE(&contexts[0],0);
    now+=10;
    if(early)return 7;
    { XV_PHASE_SCOPE(&contexts[0],1); now+=20; if(early==0)return 9; }
    return 11;
}
int main(void)
{
    reset(0);
    for(unsigned i=0;i<1000;i++) {
        assert(cleanup_return(1)==7);xv_phase_suspend(&contexts[0]);
        xv_phase_resume(&contexts[0]);xv_phase_forget(&contexts[0]);xv_phase_frame(i+1);
    }
    assert(clock_reads==0 && !owners[0].context && !stats[0].calls);

    reset(1);now=10;xv_phase_scope a,b,c;
    xv_phase_begin(&a,&contexts[0],0);now=20;xv_phase_begin(&b,&contexts[0],1);
    now=50;xv_phase_end(&b);now=60;xv_phase_suspend(&contexts[0]);
    now=70;xv_phase_begin(&c,&contexts[1],2);now=100;xv_phase_end(&c);
    now=160;xv_phase_resume(&contexts[0]);now=180;xv_phase_end(&a);
    assert(stats[0].active==70 && stats[0].self==40 && stats[0].parked==100 && stats[0].parked_self==100);
    assert(stats[1].active==30 && stats[1].self==30 && !stats[1].parked);
    assert(stats[2].active==30 && stats[2].self==30 && !stats[2].parked);
    assert(!invalid && !dropped);

    reset(1);now=100;xv_phase_begin(&a,&contexts[0],0);
    now=150;xv_phase_begin(&b,&contexts[0],1);now=200;report();
    assert(strstr(logs,"root calls 1 active-us 100 self-us 50 parked-us 0"));
    assert(strstr(logs,"child calls 1 active-us 50 self-us 50 parked-us 0"));
    now=230;xv_phase_end(&b);now=250;xv_phase_end(&a);
    assert(stats[0].active==50 && stats[0].self==20 && !stats[0].calls);
    assert(stats[1].active==30 && stats[1].self==30 && !stats[1].calls);

    reset(1);now=10;xv_phase_begin(&a,&contexts[0],3);
    now=30;xv_phase_begin(&b,&contexts[0],3);now=50;xv_phase_end(&b);
    now=110;xv_phase_end(&a);
    assert(stats[3].calls==2 && stats[3].active==120 && stats[3].self==100);
    assert(cleanup_return(1)==7 && cleanup_return(0)==9);
    assert(!find_owner(&contexts[0],0));

    reset(1);xv_phase_scope many[XV_PHASE_MAX_OWNERS+1];
    for(unsigned i=0;i<XV_PHASE_MAX_OWNERS+1;i++)xv_phase_begin(&many[i],&contexts[i],0);
    assert(dropped==1 && !many[XV_PHASE_MAX_OWNERS].owner);
    for(unsigned i=0;i<XV_PHASE_MAX_OWNERS+1;i++)xv_phase_end(&many[i]);
    xv_phase_scope deep[XV_PHASE_MAX_DEPTH+1];
    for(unsigned i=0;i<XV_PHASE_MAX_DEPTH+1;i++)xv_phase_begin(&deep[i],&contexts[0],1);
    assert(dropped==2 && !deep[XV_PHASE_MAX_DEPTH].owner);
    for(unsigned i=XV_PHASE_MAX_DEPTH+1;i-->0;)xv_phase_end(&deep[i]);
    assert(!find_owner(&contexts[0],0));
    xv_phase_begin(&a,NULL,0);xv_phase_begin(&b,&contexts[0],99);assert(dropped==4);

    reset(1);now=10;xv_phase_begin(&a,&contexts[0],0);
    now=20;xv_phase_suspend(&contexts[0]);now=40;report();
    assert(strstr(logs,"root calls 1 active-us 10 self-us 10 parked-us 20 parked-self-us 20"));
    now=50;xv_phase_forget(&contexts[0]);
    assert(stats[0].parked==10 && !find_owner(&contexts[0],0));
    xv_phase_begin(&b,&contexts[0],1);xv_phase_end(&a); /* stale cleanup cannot remove reused owner */
    assert(find_owner(&contexts[0],0)->top==&b);xv_phase_end(&b);

    reset(1);now=10;xv_phase_begin(&a,&contexts[0],0);now=20;xv_phase_begin(&b,&contexts[0],1);
    now=30;xv_phase_end(&a);xv_phase_end(&b);assert(invalid==1 && !find_owner(&contexts[0],0));
    now=50;xv_phase_begin(&a,&contexts[0],0);now=40;xv_phase_end(&a);assert(invalid==2);
    assert(stats[0].active<1000);
    puts("PASS: phase scopes, disabled fast path, nested/recursive accounting, parked guests, live reports, bounded overflow, termination and cleanup");
}
