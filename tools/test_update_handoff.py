#!/usr/bin/env python3
"""Exercise real shutdown ordering with the remote service live through drains."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
source=(root/'runtime/main.c').read_text()
start=source.index('static void xv_gfx_finish(void)')
end=source.index('static void __attribute__((unused)) xv_gfx_shutdown',start)
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "runtime/xv_update.h"
#define SCE_KERNEL_POWER_TICK_DEFAULT 0
#define SCE_DISPLAY_SETBUF_NEXTFRAME 1
typedef struct { void *ctx; } xv_gfx_t;
static xv_gfx_t g_gfx;
static unsigned updating,remote_live,step,stage,wakes,ticks;
static void log_event(const char *format,...) { (void)format; }
#define XV_LOG log_event
unsigned xv_update_requested(void) { return updating; }
void xv_update_progress(unsigned next) {
    if(!updating)return;
    assert(remote_live);
    assert((next==XV_UPDATE_GPU_DRAIN && step==0) ||
           (next==XV_UPDATE_DISPLAY_DRAIN && step==2) ||
           (next==XV_UPDATE_NETWORK_STOP && step==5));
    stage=next;
}
int scePowerRequestDisplayOn(void) { assert(updating && remote_live && !step);wakes++;return 0; }
int sceKernelPowerTick(unsigned type) { assert(!type && wakes && !step);ticks++;return 0; }
void sceGxmFinish(void *ctx) {
    assert(ctx==g_gfx.ctx && remote_live && step++==0);
    /* A stalled finish must leave the paired server able to expose this stage. */
    if(updating)assert(stage==XV_UPDATE_GPU_DRAIN && wakes && ticks);
}
void sceGxmDisplayQueueFinish(void) { assert(remote_live && step++==1); }
int sceDisplaySetFrameBuf(void *frame,unsigned mode) {
    assert(!frame && mode==SCE_DISPLAY_SETBUF_NEXTFRAME && remote_live && step++==2);
    if(updating)assert(stage==XV_UPDATE_DISPLAY_DRAIN);
    return 0;
}
int sceDisplayWaitVblankStart(void) { assert(remote_live && (step==3 || step==4));step++;return 0; }
void xv_remote_stop(void) {
    assert(remote_live && step++==5);
    if(updating)assert(stage==XV_UPDATE_NETWORK_STOP);
    remote_live=0;
}
void xv_net_shutdown(void) { assert(!remote_live && step++==6); }
'''
suffix=r'''
int main(void) {
    for(updating=0;updating<2;updating++) {
        remote_live=1;step=stage=wakes=ticks=0;g_gfx.ctx=(void*)1;
        xv_finish_for_exit();
        assert(step==7 && !remote_live && wakes==updating && ticks==updating);
    }
    puts("PASS: update wakes display before drains; GPU/display waits retain remote diagnostics; network stops before process handoff");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-update-handoff-') as directory:
    p=Path(directory);(p/'test.c').write_text(prefix+source[start:end]+suffix)
    subprocess.run(['cc','-std=gnu11','-DXV_RUN_RECOMP','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I',str(root),str(p/'test.c'),
                    '-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)

# Exercise the production lease timer with a controllable clock. It must keep
# the display and suspend timers alive only inside the requested lease.
remote=(root/'runtime/xv_remote.c').read_text()
a=remote.index('static void keep_awake(void)')
b=remote.index('static int send_all',a)
lease=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define SCE_KERNEL_POWER_TICK_DEFAULT 0
static uint64_t awake_until,now;
static unsigned ticks;
static uint64_t remote_now(void) {return now;}
static int sceKernelPowerTick(unsigned type) {assert(type==0);ticks++;return 0;}
'''
lease_main=r'''
int main(void) {
    now=100;keep_awake();assert(ticks==0);
    awake_until=3000100;keep_awake();assert(ticks==1);
    now=1000099;keep_awake();assert(ticks==1);
    now=1000100;keep_awake();assert(ticks==2);
    now=3000100;keep_awake();assert(ticks==2);
    awake_until=0;now=4000100;keep_awake();assert(ticks==2);
    awake_until=5000100;keep_awake();assert(ticks==3);
    puts("PASS: display/suspend lease ticks at most once per second and stops at expiry/cancellation");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-update-lease-') as directory:
    p=Path(directory);(p/'test.c').write_text(lease+remote[a:b]+lease_main)
    subprocess.run(['cc','-std=gnu11','-D__vita__','-Wall','-Wextra','-Werror',
                    str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
