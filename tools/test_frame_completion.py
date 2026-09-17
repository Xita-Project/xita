#!/usr/bin/env python3
"""Exercise the production pump/retirement code with delayed hardware fences.
The fake display releases scanout independently of GPU scratch retirement.
"""
import pathlib,os,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
source=(root/'runtime/main.c').read_text()
a=source.index('static unsigned g_retired_count')
b=source.index('/* ======================================================================================\n *  Mock guest workload',a)
pump=source[a:b]
a=source.index('static struct {',source.index('static volatile int      g_running'))
b=source.index('static xv_slot_owner',a)
packets=source[a:b]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <psp2/gxm.h>
#include "runtime/xv_frame_slots.h"
#include "runtime/xv_quality_settings.h"
#include "runtime/xv_render_profile.h"
#include "runtime/xv_frame_events.h"
#include "runtime/xv_packet_timing.h"
#define XV_DISPLAY_BUFFER_COUNT 3u
#define XV_DISPLAY_MAX_PENDING 2u
#define XV_LOG(...) ((void)0)
static xv_frame_events g_frame_events={-1};
int sceKernelSetEventFlag(SceUID id,unsigned bits) { assert(0); return 0; }
int sceKernelWaitEventFlag(SceUID id,unsigned bits,unsigned mode,unsigned *out,SceUInt *timeout) { assert(0); return 0; }
const unsigned xv_guest_trace_enabled=1;
void xv_logf(const char *fmt,...) {}
static volatile uint32_t g_frame_requested,g_frame_completed;
static uint32_t g_frame_submitted, g_settings_frame_period;
static volatile int g_running=1;
static uint64_t xv_pump_us_acc;
static struct { SceGxmContext *ctx; int hle_ready; unsigned front_index,back_index; void *scaled_target; } g_gfx={NULL,1,2,0,NULL};
static uint32_t g_display_free[3],g_display_queued,g_display_released;
static volatile unsigned notification_words[8];
static volatile unsigned *g_notifications=notification_words;
'''
fixture=r'''
static unsigned rendered,finished,queries,checked,peak_live,base,failed_frame;
static int early,has_queries=1,missing_world,complete_during_submit;
#ifdef XV_QUERY_BOUNDARY
static int boundary,unsupported;
static SceGxmNotification boundary_fence[3];
static int xv_query_boundary_enabled(void) {return boundary;}
int xv_d3d_query_boundary_prepare(uint32_t frame,int enabled)
{assert(frame>=10 && frame<=12);return enabled&&has_queries&&!unsupported;}
void xv_d3d_query_boundary_arm(uint32_t frame,const SceGxmNotification *fence)
{assert(*fence->address!=fence->value);boundary_fence[frame-10]=*fence;}
void xv_d3d_query_boundary_report(void) {}
#endif
#if XV_QUERY_PREFIX_PUBLISH
/* This fixture's three frames use distinct retained generations. The actual
 * history-collision guard is exercised in test_query_publication.py. */
int xv_d3d_visibility_publication_safe(uint32_t frame,const uint32_t *prior,unsigned count)
{ assert(frame>=10 && frame<=12 && count<XV_FRAME_SLOTS);return 1; }
#endif
static int xv_early_visibility_enabled(void) { return early; }
int xv_d3d_has_visibility(uint32_t frame) { assert(frame>=10 && frame<=12);return has_queries; }
static uint64_t now=1,retired_at[3],submitted_at[3],queries_at[3];
static struct { SceGxmNotification fence,world; uint64_t due,world_due,display_due; unsigned old_slot; int done,world_done,shown,accepted; } gpu[3];
void xv_cpu_log_thread(const char *role) {}
void xv_cpu_poll(uint64_t t) {}
void xv_gpu_write_barrier(void) {}
static int xv_pump_resolution(void) { return 0; }
uint64_t sceKernelGetProcessTimeWide(void) { return now; }
uint64_t xk_os_monotonic_us(void) { return now; }
void xv_d3d_visibility_complete(uint32_t frame)
{
    assert(frame==10+queries && (gpu[queries].world_done || gpu[queries].done));
#if XV_QUERY_PREFIX_PUBLISH
    assert(!xv_ticket_complete(g_frame_completed,base+queries+1u));
#else
    assert(g_frame_completed==base+queries);
#endif
    assert(!gpu[queries].shown);
    /* Early publication cannot advance the completion ticket or permit this
     * packet's frame storage/notification word to be reused. */
    if(!gpu[queries].done)assert(g_packets[(base+queries+1u)&3u].mesh==frame);
    queries_at[queries]=now;queries++;
}
void xv_d3d_check_geometry(uint32_t frame) {
    assert(frame==10+checked && gpu[checked].done && queries>checked);
    assert(g_frame_completed==base+checked);retired_at[checked++]=now;
}
static int xv_gfx_render_frame(uint32_t mesh,unsigned ui,const SceGxmNotification *fence,const SceGxmNotification *world)
{
    assert(mesh==10+rendered && ui==rendered);
    assert(g_display_free[g_gfx.back_index] && g_display_queued-g_display_released<2);
    unsigned i=rendered++;
    gpu[i].fence=*fence;submitted_at[i]=now;
    int prefix=0;
#ifdef XV_QUERY_BOUNDARY
    prefix=boundary_fence[i].address!=NULL;
    if(prefix){gpu[i].world=boundary_fence[i];assert(*gpu[i].world.address!=gpu[i].world.value);}
#endif
    assert((world!=NULL)==(!prefix && early && g_gfx.scaled_target && has_queries));
    if(world) {gpu[i].world=*world;assert(*world->address!=world->value);}
    /* Work completion remains delayed after CPU submission. */
    gpu[i].world_due=now+5000;gpu[i].due=now+10000;gpu[i].display_due=now+12000;
    gpu[i].old_slot=g_gfx.front_index;
    if(i+1==failed_frame)return -7;
    gpu[i].accepted=1;
    g_display_free[g_gfx.back_index]=0;
    g_display_queued++;
    g_gfx.front_index=g_gfx.back_index;g_gfx.back_index=(g_gfx.back_index+1)%3;
    if(rendered-checked>peak_live)peak_live=rendered-checked;
    if(complete_during_submit) {
        now+=2000;gpu[i].done=1;*gpu[i].fence.address=gpu[i].fence.value;
        gpu[i].due=now;now+=2000; /* notification precedes CPU return */
    }
    return 0;
}
void sceGxmFinish(SceGxmContext *ctx)
{
    assert(failed_frame && rendered==failed_frame);finished++;
    for(unsigned i=0;i<rendered;i++) {
        gpu[i].done=1;*gpu[i].fence.address=gpu[i].fence.value;
    }
}
int sceKernelDelayThread(SceUInt delay)
{
    now+=delay;assert(now<1000000);
    for(unsigned i=0;i<rendered;i++) {
        if(gpu[i].world.address && !missing_world && !gpu[i].world_done && now>=gpu[i].world_due) {
            gpu[i].world_done=1;*gpu[i].world.address=gpu[i].world.value;
        }
        if(!gpu[i].done && now>=gpu[i].due) {gpu[i].done=1;*gpu[i].fence.address=gpu[i].fence.value;}
        if(gpu[i].accepted && !gpu[i].shown && now>=gpu[i].display_due) {
            assert(gpu[i].done);gpu[i].shown=1;
            g_display_free[gpu[i].old_slot]=1;g_display_released++;
        }
    }
    if(checked==3) {assert(g_frame_completed==base+3u);g_running=0;}
    return 0;
}
'''
suffix=r'''
int main(int argc,char **argv)
{
    int capped=argc>1 && atoi(argv[1]);
    if(capped)setenv("XV_FRAME_CAP","20",1);else unsetenv("XV_FRAME_CAP");
    base=argc>2 ? UINT32_MAX-1u : 0;
    failed_frame=argc>3 ? 2 : 0;
    early=getenv("TEST_EARLY")!=NULL;
#ifdef XV_QUERY_BOUNDARY
    boundary=getenv("TEST_BOUNDARY")!=NULL;unsupported=getenv("TEST_UNSUPPORTED")!=NULL;
#endif
    g_gfx.scaled_target=getenv("TEST_NATIVE")?NULL:(void*)1;
    has_queries=!getenv("TEST_NO_QUERIES");missing_world=getenv("TEST_MISSING_WORLD")!=NULL;
    complete_during_submit=getenv("TEST_COMPLETE_DURING_SUBMIT")!=NULL;
    g_frame_completed=g_frame_submitted=base;g_frame_requested=base+3u;
    for(unsigned i=0;i<3;i++) {
        uint32_t ticket=base+i+1u;unsigned q=ticket&3u;
        g_packets[q].mesh=10+i;g_packets[q].ui=i;
        notification_words[4+q]=ticket; /* stale matching query value must be cleared before submission */
        notification_words[q]=ticket-4u; /* previous use, including ticket wrap */
    }
    xv_pump_thread(0,NULL);
#if XV_GPU_PACKET_TIMING
    assert(g_packet_timing.retired==3 && g_packet_timing.failed==(failed_frame?1u:0u));
    assert(g_packet_timing.valid==(failed_frame?2u:3u) && !g_packet_timing.invalid);
    if(complete_during_submit) {
        assert(g_packet_timing.no_negative==3 && g_packet_timing.sum[XV_PACKET_SUBMIT]==12000);
        assert(g_packet_timing.sum[XV_PACKET_LOWER]==0 && g_packet_timing.sum[XV_PACKET_UPPER]==12000);
        assert(g_packet_timing.sum[XV_PACKET_TAIL_LOWER]==0 && g_packet_timing.sum[XV_PACKET_TAIL_UPPER]==0);
    }
#endif
#ifdef XV_QUERY_BOUNDARY
    if(boundary && has_queries && !unsupported && !failed_frame && !complete_during_submit) {
        assert(g_boundary_queries==(missing_world?0u:3u));
        assert(g_boundary_fallbacks==(missing_world?3u:0u));
        assert(missing_world?!g_boundary_before_final:g_boundary_before_final>0);
    }
#endif
    assert(rendered==3 && queries==3 && checked==3 && finished==(failed_frame?1u:0u));
    if(!failed_frame && !complete_during_submit) {
        if(capped) {assert(peak_live==1);assert(retired_at[0]<submitted_at[1]);}
        else {assert(peak_live>=2);assert(submitted_at[1]<retired_at[0]);}
        assert(retired_at[0]<=gpu[0].due+100);
        int prefix=0;
#ifdef XV_QUERY_BOUNDARY
        prefix=boundary && has_queries && !unsupported;
#endif
        if((prefix || (early && g_gfx.scaled_target && has_queries)) && !missing_world) {
            assert(queries_at[0]<=gpu[0].world_due+100);
            assert(queries_at[0]<retired_at[0]);
        } else assert(queries_at[0]==retired_at[0]);
        assert(retired_at[0]<gpu[0].display_due); /* retirement need not wait for vblank */
    }
    puts("PASS: world query completion never releases frame storage; final retirement, scanout, pacing, ticket wrap, missing query notification and failure drain");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-frame-completion-') as tmp:
    p=pathlib.Path(tmp);(p/'test.c').write_text(prefix+packets+fixture+pump+suffix)
    sdk=pathlib.Path(os.environ.get('VITASDK',str(pathlib.Path.home()/'vitasdk')))
    subprocess.run(['cc','-std=gnu11','-DXV_RUN_RECOMP','-DXV_GPU_PACKET_TIMING='+os.environ.get('TEST_GPU_PACKET_TIMING','0'),*(['-DXV_QUERY_BOUNDARY'] if os.environ.get('TEST_QUERY_BOUNDARY') else []),'-Wall','-Wextra','-Werror','-Wno-unused-parameter',
      *(['-DXV_QUERY_PREFIX_PUBLISH=1','-DXV_FLARE_QUERY_OVERLAP'] if os.environ.get('TEST_QUERY_PREFIX_PUBLISH')=='1' else []),
      '-I',str(root),'-I',str(root/'runtime'),'-idirafter',str(sdk/'arm-vita-eabi/include'),str(p/'test.c'),str(root/'runtime/xv_render_profile.c'),'-o',str(p/'test')],check=True)
    for mode in [[],['TEST_EARLY'],['TEST_EARLY','TEST_NATIVE'],['TEST_EARLY','TEST_NO_QUERIES'],['TEST_EARLY','TEST_MISSING_WORLD'],['TEST_BOUNDARY','TEST_NATIVE'],['TEST_BOUNDARY','TEST_EARLY'],['TEST_BOUNDARY','TEST_UNSUPPORTED','TEST_EARLY'],['TEST_BOUNDARY','TEST_NO_QUERIES'],['TEST_BOUNDARY','TEST_MISSING_WORLD']]:
        env=os.environ.copy();env.update({key:'1' for key in mode})
        for args in [[],['1'],['0','wrap'],['1','wrap'],['0','wrap','error']]:
            subprocess.run([str(p/'test'),*args],check=True,env=env)
    subprocess.run([str(p/'test'),'0','wrap'],check=True,
                   env={**os.environ,'TEST_COMPLETE_DURING_SUBMIT':'1'})
