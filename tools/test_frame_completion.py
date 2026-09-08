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
static struct { SceGxmContext *ctx; int hle_ready; unsigned front_index,back_index; } g_gfx={NULL,1,2,0};
static uint32_t g_display_free[3],g_display_queued,g_display_released;
static volatile unsigned notification_words[4];
static volatile unsigned *g_notifications=notification_words;
'''
fixture=r'''
static unsigned rendered,finished,queries,peak_live,base,failed_frame;
static uint64_t now=1,retired_at[3],submitted_at[3];
static struct { SceGxmNotification fence; uint64_t due,display_due; unsigned old_slot; int done,shown,accepted; } gpu[3];
void xv_cpu_log_thread(const char *role) {}
void xv_cpu_poll(uint64_t t) {}
void xv_gpu_write_barrier(void) {}
static int xv_pump_resolution(void) { return 0; }
uint64_t sceKernelGetProcessTimeWide(void) { return now; }
uint64_t xk_os_monotonic_us(void) { return now; }
void xv_d3d_visibility_complete(uint32_t frame)
{
    assert(frame==10+queries && gpu[queries].done);
    assert(g_frame_completed==base+queries);
    retired_at[queries]=now;queries++;
}
void xv_d3d_check_geometry(uint32_t frame) { assert(frame==9+queries && gpu[queries-1].done); }
static int xv_gfx_render_frame(uint32_t mesh,unsigned ui,const SceGxmNotification *fence)
{
    assert(mesh==10+rendered && ui==rendered);
    assert(g_display_free[g_gfx.back_index] && g_display_queued-g_display_released<2);
    unsigned i=rendered++;
    gpu[i].fence=*fence;submitted_at[i]=now;
    /* Work completion remains delayed after CPU submission. */
    gpu[i].due=now+10000;gpu[i].display_due=now+12000;
    gpu[i].old_slot=g_gfx.front_index;
    if(i+1==failed_frame)return -7;
    gpu[i].accepted=1;
    g_display_free[g_gfx.back_index]=0;
    g_display_queued++;
    g_gfx.front_index=g_gfx.back_index;g_gfx.back_index=(g_gfx.back_index+1)%3;
    if(rendered-queries>peak_live)peak_live=rendered-queries;
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
        if(!gpu[i].done && now>=gpu[i].due) {gpu[i].done=1;*gpu[i].fence.address=gpu[i].fence.value;}
        if(gpu[i].accepted && !gpu[i].shown && now>=gpu[i].display_due) {
            assert(gpu[i].done);gpu[i].shown=1;
            g_display_free[gpu[i].old_slot]=1;g_display_released++;
        }
    }
    if(queries==3) {assert(g_frame_completed==base+3u);g_running=0;}
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
    g_frame_completed=g_frame_submitted=base;g_frame_requested=base+3u;
    for(unsigned i=0;i<3;i++) {
        uint32_t ticket=base+i+1u;unsigned q=ticket&3u;
        g_packets[q].mesh=10+i;g_packets[q].ui=i;
        notification_words[q]=ticket-4u; /* previous use, including ticket wrap */
    }
    xv_pump_thread(0,NULL);
    assert(rendered==3 && queries==3 && finished==(failed_frame?1u:0u));
    if(!failed_frame) {
        if(capped) {assert(peak_live==1);assert(retired_at[0]<submitted_at[1]);}
        else {assert(peak_live>=2);assert(submitted_at[1]<retired_at[0]);}
        assert(retired_at[0]<=gpu[0].due+100);
        assert(retired_at[0]<gpu[0].display_due); /* retirement need not wait for vblank */
    }
    puts("PASS: production notification retirement, overlapping submissions, scanout ownership, pacing, ticket wrap and exceptional failure drain");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-frame-completion-') as tmp:
    p=pathlib.Path(tmp);(p/'test.c').write_text(prefix+packets+fixture+pump+suffix)
    sdk=pathlib.Path(os.environ.get('VITASDK',str(pathlib.Path.home()/'vitasdk')))
    subprocess.run(['cc','-std=gnu11','-DXV_RUN_RECOMP','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
      '-I',str(root),'-I',str(root/'runtime'),'-idirafter',str(sdk/'arm-vita-eabi/include'),str(p/'test.c'),str(root/'runtime/xv_render_profile.c'),'-o',str(p/'test')],check=True)
    for args in [[],['1'],['0','wrap'],['1','wrap'],['0','wrap','error']]:
        subprocess.run([str(p/'test'),*args],check=True)
