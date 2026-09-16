/* Production state machine; controls/time are fallible owner-boundary doubles. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../../runtime/xv_benchmark.c"
static char output[131072];static size_t used;
static uint64_t clock_us;
static unsigned drains,controls,takes;
static int native_owner=1,capability=1,fail_control,fail_take,fail_persistent;
static const float camera[6]={1,2,3,1,0,0};
#ifdef XV_LIGHT_QUERY_CENSUS
unsigned xv_light_census_enabled,xv_light_census_present_requested;
static int region_mode,region_inits;
void xv_native_clip_region_init(void){region_inits++;}
int xv_native_clip_region_available(void){return 1;}
int xv_native_clip_region_enabled(void){return region_mode;}
void xv_native_clip_region_override(int mode){region_mode=mode>0;}
static xctx context;
static xv_light_census_stats counts;
int xv_object_census_is_owner(void){return native_owner;}
xctx *xv_light_census_present_current(void){return native_owner&&capability?&context:NULL;}
int xv_light_census_control(xctx*c,int enabled,int reset)
{
 assert(c==&context&&native_owner&&capability);controls++;
 if(fail_control&&(controls==(unsigned)fail_control||(fail_persistent&&controls>=(unsigned)fail_control)))return 0;
 xv_light_census_enabled=enabled;if(reset)memset(&counts,0,sizeof counts);return 1;
}
int xv_light_census_take(xctx*c,xv_light_census_stats*out,int reset)
{
 assert(c==&context&&native_owner&&capability&&!reset);takes++;
 if(fail_take&&takes==(unsigned)fail_take)return 0;
 *out=counts;out->group_storage_bytes=192;out->stats_storage_bytes=1216;return 1;
}
uint64_t xv_benchmark_boundary_time(void){return clock_us;}
static void frame_counts(void)
{
 if(!xv_light_census_enabled)return;
 counts.entries+=3;counts.opened+=3;counts.completed+=3;counts.candidates+=2;
 counts.queries+=6;counts.traversals+=6;counts.removals++;
 counts.multi_groups+=2;counts.multi_traversals+=6;counts.candidate_multi_groups++;counts.candidate_multi_traversals+=4;
 counts.orphan_queries+=2;counts.admission[0][XV_LC_OK]+=3;counts.admission[1][XV_LC_OK]+=8;counts.admission[2][XV_LC_OK]++;
 counts.admission[0][XV_LC_WORKER]+=7;counts.admission[1][XV_LC_WORKER]+=14;counts.declined[XV_LC_WORKER]+=21;
 counts.guest_reads+=100;counts.guest_bytes+=400;
}
#endif
void xv_logf(const char *fmt,...)
{
 va_list a;va_start(a,fmt);int n=vsnprintf(output+used,sizeof output-used,fmt,a);va_end(a);
 assert(n>=0&&(size_t)n<sizeof output-used);used+=(size_t)n;clock_us+=2000;
}
void xv_benchmark_optimizations(int enabled)
{(void)enabled;assert(xv_benchmark_compare_light_census());drains++;clock_us+=40000;}
static void reset(int initial)
{
 memset(&b,0,sizeof b);status=request_state=remote_ready=remote_kind=0;
 drains=controls=takes=0;native_owner=capability=1;fail_control=fail_take=fail_persistent=0;
 clock_us=1;used=0;output[0]=0;
#ifdef XV_LIGHT_QUERY_CENSUS
 region_mode=region_inits=0;
 xv_light_census_enabled=initial;xv_light_census_present_requested=0;memset(&counts,0,sizeof counts);
#else
 (void)initial;
#endif
}
/* Compile the unmodified production Present controller from main.c. */
static struct {unsigned render_height;} g_gfx={544};
static unsigned g_resolution_result,g_resolution_request,g_frame_events,present_requests;
static int view_valid=1,move_camera;
enum {XV_FRAME_REQUESTED=1,XV_FRAME_COMPLETED=2};
static uint64_t sceKernelGetProcessTimeWide(void){return clock_us;}
int xd3d_benchmark_view(float view[6])
{
#ifdef XV_LIGHT_QUERY_CENSUS
 assert(native_owner&&capability); /* admission must precede guest reads */
#endif
 memcpy(view,camera,sizeof camera);if(move_camera)view[0]+=1;return view_valid;
}
static void xv_present_drain(void){}
static void xv_frame_events_signal(unsigned*events,unsigned flag)
{(void)events;assert(flag==XV_FRAME_REQUESTED);g_resolution_result=g_resolution_request;present_requests++;}
static void xv_frame_events_wait(unsigned*events,unsigned flag,unsigned timeout)
{(void)events;(void)flag;(void)timeout;assert(!"unexpected wait");}
#include "present.inc"
static unsigned step(unsigned delta,int valid)
{
 clock_us+=delta;view_valid=valid;unsigned before=present_requests;
#ifdef XV_LIGHT_QUERY_CENSUS
 if(native_owner&&capability)frame_counts();
#endif
 xv_benchmark_present();return present_requests!=before?g_resolution_result:0;
}
static void request(void)
{
 xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_LIGHT_CENSUS));
 assert(!controls);xv_benchmark_remote_poll(1);assert(!controls);
#ifdef XV_LIGHT_QUERY_CENSUS
 assert(xv_light_census_present_requested);
#endif
}
static void complete(void)
{unsigned n=0;while(b.active){step(100000,1);assert(++n<700);}}
int main(int argc,char**argv)
{
#ifndef XV_LIGHT_QUERY_CENSUS
 (void)argc;(void)argv;reset(0);xv_benchmark_remote_poll(1);
 assert(xv_benchmark_remote_request(XV_BENCH_LIGHT_CENSUS)<0&&!b.active&&!controls);
 puts("PASS: compiled-OFF census request unavailable, no observer APIs linked");
#else
 for(int initial=0;initial<=1;initial++){
  reset(initial);request();assert(step(1,1)==544&&xv_light_census_enabled==0);complete();
  assert(!xv_benchmark_remote_busy()&&!xv_light_census_present_requested&&xv_light_census_enabled==(unsigned)initial);
  assert(controls==7&&takes==3&&drains==10);
  assert(b.fps[0]==10&&b.fps[1]==10&&b.fps[2]==10);
  assert(strstr(output,"phase 2 groups 360/360/360/0/240")&&strstr(output,"phase 2 work 720/720/120/240/720/120/480"));
  assert(strstr(output,"phase 2 entry-admission 360/0/840/0/0/0/0/0"));
  assert(strstr(output,"result off-before 10.000 on 10.000 off-after 10.000 fps comparable-view 1"));
  if(argc>1&&initial==0){FILE*f=fopen(argv[1],"w");assert(f);assert(fwrite(output,1,used,f)==used);assert(!fclose(f));}
 }
 // The observer cannot silently force clip fallback in only its ON arm;
 // likewise a clip comparison cannot label entirely declined work as ON.
 reset(0);region_mode=1;request();assert(!step(1,1));
 assert(!b.active&&!xv_benchmark_remote_busy()&&!xv_light_census_present_requested&&region_mode==1&&!controls);
 reset(1);xv_benchmark_remote_poll(1);assert(!xv_benchmark_remote_request(XV_BENCH_CLIP_REGION));
 xv_benchmark_remote_poll(1);assert(!step(1,1));
 assert(!b.active&&!xv_benchmark_remote_busy()&&xv_light_census_enabled==1&&!region_inits&&!controls);
 // Network admission never initializes/reads guest state. Foreign Present
 // cannot consume or restore an owner's active request.
 reset(1);request();native_owner=0;step(1,1);assert(b.request&&xv_benchmark_remote_busy()&&!controls);
 native_owner=1;capability=0;step(1,1);assert(!b.request&&!xv_benchmark_remote_busy()&&xv_light_census_enabled==1&&!controls&&!xv_light_census_present_requested);
 reset(1);request();step(1,0);assert(!b.active&&!controls&&xv_light_census_enabled==1&&!xv_benchmark_remote_busy());
 reset(1);request();xv_benchmark_compare_toggle();assert(!xv_benchmark_remote_busy()&&!xv_light_census_present_requested&&xv_light_census_enabled==1);
 for(unsigned cause=0;cause<3;cause++){
  reset(0);request();step(1,1);while(b.phase==0)step(100000,1);assert(xv_light_census_enabled==1);
  if(cause==0)xv_benchmark_compare_toggle();
  if(cause==2){capability=0;step(100000,1);assert(b.restoring&&xv_benchmark_remote_busy()&&xv_light_census_enabled==1);capability=1;}
  step(300000,cause!=1);assert(!b.active&&!xv_benchmark_remote_busy()&&!xv_light_census_present_requested&&xv_light_census_enabled==0);
 }
 // Every reset/enable transition failure restores, without valid results.
 for(int fail=1;fail<=6;fail++){
  reset(1);fail_control=fail;request();step(1,1);complete();
  assert(!xv_benchmark_remote_busy()&&xv_light_census_enabled==1&&strstr(output,"boundary failure")&&!strstr(output,"] result"));
 }
 for(int fail=1;fail<=3;fail++){
  reset(0);fail_take=fail;request();step(1,1);complete();
  assert(!xv_benchmark_remote_busy()&&!xv_light_census_enabled&&strstr(output,"boundary failure")&&!strstr(output,"] result"));
 }
 reset(0);request();step(1,1);while(b.phase==0)step(100000,1);
 fail_control=controls+1;fail_persistent=1;xv_benchmark_compare_toggle();step(100000,1);
 assert(b.restoring&&xv_benchmark_remote_busy()&&xv_benchmark_status()&&xv_light_census_enabled==1&&!strstr(output,"restored 544p"));
 unsigned prior=controls;step(100000,1);assert(controls==prior);step(200000,1);assert(controls==prior+1&&b.active);
 fail_control=0;step(300000,1);assert(!b.active&&!xv_benchmark_remote_busy()&&!xv_light_census_enabled);
 reset(0);request();step(1,1);while(b.frames<SETTLE)step(100000,1);move_camera=1;complete();move_camera=0;
 assert(strstr(output,"comparable-view 0")&&!xv_light_census_enabled&&!xv_benchmark_remote_busy());
 // Worst representable counters still fit the production logger's 512-byte
 // record, including 28 uint32 decline reasons and 17 uint64 source counts.
 reset(1);b.phase=1;remote_kind=XV_BENCH_LIGHT_CENSUS;memset(&counts,255,sizeof counts);
 assert(!census_boundary(0));
 for(const char*p=output;*p;){const char*end=strchr(p,'\n');assert(end&&end-p<512);p=end+1;}
 reset(0);request();step(1,1);xv_light_census_enabled=1;while(b.active)step(100000,1);
 assert(strstr(output,"boundary failure")&&!strstr(output,"] result")&&!xv_light_census_enabled);
 puts("PASS: production census benchmark admission, exact 120-frame counts/time exclusions, initial OFF/ON restoration, cancellation/context loss, reset/take/mode/restore failures and busy retries");
#endif
 return 0;
}
