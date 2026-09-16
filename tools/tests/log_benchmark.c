/* Exercise the production benchmark state machine with fallible owner controls. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/xv_benchmark.c"
static char output[32768];static size_t used;
static unsigned inits,sets,drains;static int mode,capable=1,init_error,fail_from;
static uint64_t clock_us;
static const float camera[6]={1,2,3,1,0,0};
void xv_logf(const char *fmt,...)
{va_list a;va_start(a,fmt);int n=vsnprintf(output+used,sizeof output-used,fmt,a);va_end(a);assert(n>=0&&(size_t)n<sizeof output-used);used+=(size_t)n;}
void xv_benchmark_optimizations(int enabled)
{(void)enabled;assert(xv_benchmark_compare_log_writer());drains++;}
#ifndef TEST_NO_LOG_API
int xv_log_async_available(void) {return capable;}
int xv_log_async_init(void) {inits++;return init_error;}
int xv_log_async_enabled(void) {return mode;}
int xv_log_async_set_enabled(int value,unsigned timeout)
{assert(timeout==5000000&&drains==sets+1);sets++;if(fail_from&&(int)sets>=fail_from)return -2;mode=value;return 0;}
#endif
static void reset(int initial)
{memset(&b,0,sizeof b);status=request_state=remote_ready=remote_kind=0;mode=initial;inits=sets=drains=0;capable=1;init_error=fail_from=0;clock_us=1;used=0;output[0]=0;}
static unsigned step(unsigned delta,int valid)
{clock_us+=delta;unsigned height=xv_benchmark_step(clock_us,544,valid,camera);if(height)xv_benchmark_applied(clock_us,height);return height;}
static void begin(void)
{xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_LOG_WRITER));assert(!inits);xv_benchmark_remote_poll(1);assert(!inits);assert(step(1,1)==544);}
static void advance_to_on(void)
{while(b.active&&b.phase==0)step(100000,1);assert(b.active&&b.phase==1&&mode==1);}
int main(void)
{
#ifdef TEST_NO_LOG_API
 reset(0);xv_benchmark_remote_poll(1);assert(xv_benchmark_remote_request(XV_BENCH_LOG_WRITER)<0);assert(!inits&&!sets&&!b.active);puts("PASS: absent logger APIs reject without initialization");
#else
 for(int initial=0;initial<=1;initial++) {
  reset(initial);begin();assert(inits==1&&mode==0);
  unsigned iterations=0;
  while(b.active) {
   unsigned dt=b.frames==SETTLE?4000000u:(b.frames>SETTLE&&(b.frames-SETTLE)%100==0?300000u:100000u);
   step(dt,1);assert(++iterations<6000);
  }
  assert(mode==initial&&sets==4&&!xv_benchmark_remote_busy());
  const char *s=output;unsigned summaries=0;
  while((s=strstr(s,"[log-writer-pacing]"))) {
   unsigned phase,n,a,z,c,d;unsigned long long sum,lo,p50,p95,p99,p999,hi;
   assert(sscanf(s,"[log-writer-pacing] phase %u samples %u sum-us %llu min/p50/p95/p99/p999/max-us %llu/%llu/%llu/%llu/%llu/%llu over50/100/150/200ms %u/%u/%u/%u",&phase,&n,&sum,&lo,&p50,&p95,&p99,&p999,&hi,&a,&z,&c,&d)==13);
   assert(n==1799&&lo==100000&&p50==100000&&p95==100000&&hi==300000&&p999==300000);
   assert(a==1799&&z==17&&c==17&&d==17&&sum==183300000);summaries++;s++;
  }
  assert(summaries==3&&strstr(output,"1800 frames elapsed-us 187300000")&&strstr(output,"restored 544p"));
 }
 reset(1);xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_LOG_WRITER));xv_benchmark_remote_poll(1);step(1,0);assert(!inits&&mode==1&&!xv_benchmark_remote_busy());
 reset(1);capable=0;xv_benchmark_remote_poll(1);assert(xv_benchmark_remote_request(XV_BENCH_LOG_WRITER)<0);assert(!inits&&mode==1);
 reset(1);init_error=-5;xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_LOG_WRITER));xv_benchmark_remote_poll(1);assert(!step(1,1)&&inits==1&&!sets&&mode==1&&!xv_benchmark_remote_busy());
 for(unsigned failure=0;failure<3;failure++) {
  reset(0);begin();advance_to_on();
  if(failure==0)xv_benchmark_compare_toggle();
  step(100000,failure!=1);
  if(failure<2) {assert(!b.active&&mode==0&&!xv_benchmark_remote_busy());continue;}
  fail_from=sets+1;
  while(!b.restoring)step(100000,1);
  assert(mode==1&&xv_benchmark_status()&&xv_benchmark_remote_busy()&&!strstr(output,"restored 544p"));
  unsigned calls=sets;step(100000,1);assert(sets==calls);step(200000,1);assert(sets==calls+1&&mode==1);
  fail_from=0;step(300000,1);assert(!b.active&&mode==0&&!xv_benchmark_remote_busy());assert(strstr(output,"boundary failure")&&strstr(output,"restored 544p"));
 }
 reset(1);fail_from=1;begin();assert(b.restoring&&mode==1&&xv_benchmark_status()&&xv_benchmark_remote_busy());fail_from=0;step(300000,1);assert(!b.active&&mode==1&&!xv_benchmark_remote_busy());
 puts("PASS: owner-only admission,1800-frame arms, exact frame tails, initial-mode restoration, cancellation/view loss and fallible drain recovery");
#endif
 return 0;
}
