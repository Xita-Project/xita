#!/usr/bin/env python3
"""Exercise Halo's real lobby initializer, start request, and countdown locally."""
from pathlib import Path
import os, re, shlex, subprocess, sys
root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'tools'))
from patch_split_screen import OLD, NEW, PAUSE_OLD, PAUSE_NEW, STATUS_PATCHES
names = ['0009C430','0009C920','0009D110','0009BE20','0009BEA0',
         '0009BFB0','0009BAA0','0009BAD0','0009C710','0009C6D0','0009C750','000C74A0']
parts = {}
for p in (root/'recomp').glob('code_*.c'):
    s = p.read_text()
    for n in names:
        m = re.search(r'^void f_'+n+r'\(.*?^\}', s, re.M|re.S)
        if m: parts[n] = m[0]
assert len(parts) == len(names), 'Generate the user-owned Halo 3925 functions first'
assert NEW in parts['0009C430']
status_original=parts['000C74A0'].replace(STATUS_PATCHES[0][1],STATUS_PATCHES[0][0]).replace('f_000C74A0(', 'original_status(')
pause_body=PAUSE_NEW
assert sum(p.read_text().count(PAUSE_NEW) for p in (root/'recomp').glob('code_*.c'))==7
original = parts['0009C430'].replace(NEW, OLD).replace('f_0009C430(', 'original_init(')
head = r'''
#define xv_preempt test_preempt
#include "xv_recomp_protos.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
uint8_t *g_xram, *g_img_base; uint32_t *g_xpt;
volatile uint32_t xv_cur_fn;
static unsigned now, starts;
int xk_file_in_ui_map;
void xv_preempt(xctx *c) { c->preempt = 1000000; }
void f_00019840(xctx *c) { strncpy((char*)X_G(X_ARG(0)),(char*)X_G(X_ARG(1)),X_ARG(2)); X_RET(0); }
void f_0001B712(xctx *c) { memcpy(X_G(X_ARG(0)),X_G(X_ARG(1)),X_ARG(2)); X_RET(0); }
void f_0009E290(xctx *c) { X_RET(0); }
void f_00013030(xctx *c) { c->r[0] = now; X_RET(0); }
void f_0009E8E0(xctx *c) { c->r[0] = 0; X_RET(0); }
void f_0009D010(xctx *c) { assert(0); }
void f_0009CE60(xctx *c) { starts++; c->r[0] = 1; X_RET(0); }
void f_00173360(xctx *c) { c->r[0] = 0; X_RET(1); }
void f_0017CB60(xctx *c) { c->r[0] = 1; X_RET(1); }
void f_0004E000(xctx *c) { assert(0); }
'''
body = r'''
static const uint32_t host = 0x20000;
static void setup(unsigned link, unsigned players, unsigned blocked) {
 memset(g_xram,0,4<<20); now=100; starts=0;
 X_M32(host)=0x8000; X_M32(0x2E3628)=host; X_M8(0x2E3630)=link;
 xctx c={0}; c.r[4]=0x10000; X_M32(0x10004)=host; f_0009C430(&c);
 X_M16(host+0x22C)=players; X_M8(host+0x495)=blocked;
 for(unsigned i=0;i<4;i++) {
  X_M32(host+0x43C+16*i)=0;
  X_M16(host+0x448+16*i)=0xFFFF;
 }
 for(unsigned i=0;i<16;i++) {
  X_M8(host+0x24A+32*i)=0xFF; X_M8(host+0x24B+32*i)=0xFF;
 }
 for(unsigned i=0;i<players;i++) {
  X_M32(host+0x43C+16*i)=0x8100+32*i;
  X_M16(host+0x448+16*i)=i; X_M8(host+0x44A+16*i)=8;
  X_M8(host+0x24A+32*i)=i; X_M8(host+0x24B+32*i)=0;
 }
}
int main(void) {
 g_xram=calloc(1,4<<20);g_img_base=g_xram;g_xpt=calloc(1<<20,4);
 for(unsigned i=0;i<1024;i++)g_xpt[i]=i*4096;
 for(unsigned link=0;link<2;link++) {
  setup(link,1,0);
  xctx c={0};c.r[4]=0x10000;X_M32(0x10004)=host;
  original_init(&c);uint8_t before[0x600];memcpy(before,X_G(host),sizeof before);
  xctx patched={0};patched.r[4]=0x10000;f_0009C430(&patched);
  assert(c.r[4]==0x10008 && !memcmp(c.r,patched.r,sizeof c.r));
  assert(XF_C(&c)==XF_C(&patched) && XF_Z(&c)==XF_Z(&patched) && XF_S(&c)==XF_S(&patched) && XF_O(&c)==XF_O(&patched));
  assert(before[0x115]==2);before[0x115]=link?2:1;
  assert(!memcmp(before,X_G(host),sizeof before));
  for(unsigned players=0;players<=2;players++)for(unsigned blocked=0;blocked<2;blocked++) {
   setup(link,players,blocked);
   xctx request={0};request.r[4]=0x10000;request.r[1]=host;request.r[0]=1;request.preempt=1000000;
   f_0009C920(&request);assert(request.r[4]==0x10004);
   /* The status message must agree with readiness, including the empty lobby. */
   xctx status={0}; status.r[4]=0x10000; status.r[1]=0x9000; status.preempt=1000000;
   original_status(&status);
   assert(status.r[4]==0x10004);
   unsigned old_visible=X_M8(0x9010);
   status.r[4]=0x10000; status.r[1]=0x9000;
   f_000C74A0(&status);
   assert(status.r[4]==0x10004);
   if(!link) assert(X_M8(0x9010)==(players<1));
   else assert(X_M8(0x9010)==old_visible);
   unsigned can_start=!blocked && players>=(link?2:1);
   assert(X_M8(host+0x494)==can_start);
   now=40000;xctx tick={0};tick.r[0]=host;tick.r[4]=0x10000;tick.preempt=1000000;
   f_0009D110(&tick);assert(tick.r[4]==0x10004);assert(starts==can_start);
  }
 }
 puts("Split-screen lobby: initializer ABI, zero/one/two players, blocked child menus, normal countdown, and unchanged System Link passed");
 for(unsigned ui=0;ui<2;ui++)for(unsigned requested=0;requested<2;requested++)
 for(unsigned name_ok=0;name_ok<2;name_ok++) {
  uint32_t tags[]={0xE36801F2u,0xE36901F3u,0xE3CE0258u,0x12345678u};
  for(unsigned i=0;i<4;i++) {
   xctx c={0};c.r[0]=0x12340000u|requested;c.r[3]=tags[i];c.r[6]=0x9000;c.r[5]=0xA000;
   memset(X_G(0xA004),0,32);
   strcpy(X_G(0xA004),name_ok ? (i==0 ? "splitscreen_pregame_wrapper" : "splitscreen_pregame_screen") : "campaign_pause");
   xctx before=c;
   xk_file_in_ui_map=ui;test_pause(&c);
   assert(!memcmp(c.r,before.r,sizeof c.r));
   assert(X_M8(0x9013)==(name_ok&&i<2?0:requested));
  }
 }
 puts("PASS: solo/System Link status, lobby tag/name guard despite map preloading, and pause flag/register preservation");
 free(g_xpt);free(g_xram);return 0;
}
'''
p=root/'recomp/host/build/split_screen_test.c';p.parent.mkdir(exist_ok=True)
p.write_text(head+'\n'+status_original+'\nvoid test_pause(xctx *c) {\n'+pause_body+'\n}\n'+original+'\n'+'\n'.join(parts.values())+body)
exe=p.with_suffix('')
subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O2','-I'+str(root/'recomp'),'-ffunction-sections','-fdata-sections',*shlex.split(os.environ.get('SPLIT_TEST_CFLAGS','')),str(p),str(root/'recomp/xv_x86rt.c'),'-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
