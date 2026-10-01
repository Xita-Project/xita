#!/usr/bin/env python3
"""Exercise production phase scopes with deterministic clocks and thread identity."""
from pathlib import Path
import os, subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'recomp/kernel/xk_scene_thread.c').read_text()
a=s.index('#define PHASE_MAX ');b=s.index('#if defined(XV_SCENE_THREAD)',a)
fixture=r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
static int helper;
static unsigned clock_calls;
static uint64_t xk_os_monotonic_us(void) {return ++clock_calls * 10u;}
int xv_scene_thread_on_helper(void) {return helper;}
#define XK_LOG(...) ((void)0)
@BODY@
int main(int argc,char **argv) {
 assert(argc==3);int mode=atoi(argv[1]);setenv("XV_SCENE_PHASES",argv[1],1);
 setenv("XV_TICK_PHASES",argv[2],1);
 if(mode>0)mode=mode==2?2:1;
 if(mode<=0 && atoi(argv[2])>0) mode=atoi(argv[2])>=3?4:atoi(argv[2])>=2?3:2;
 if(mode==4) {
  xv_scene_phase_begin(0xFA920);xv_scene_phase_begin(0x900E0);
  for(unsigned i=0;i<10000;i++) {
   unsigned before=clock_calls;
   xv_scene_phase_begin(0x8FB70);
   unsigned selected=object_sample_active;
   assert(object_sample_depth==1);
   helper=1;xv_scene_phase_begin(0x8FB70);xv_scene_phase_end(0x8FB70);helper=0;
   assert(object_sample_depth==1);
   xv_scene_phase_begin(0x8DDF0);xv_scene_phase_end(0x8DDF0);
   xv_scene_phase_begin(0x90950);
   xv_scene_phase_begin(0x4C980);xv_scene_phase_end(0x4C980);
   xv_scene_phase_end(0x90950);
   xv_scene_phase_begin(0x8FB70);
   assert(object_sample_depth==2 && object_sample_active==selected);
   xv_scene_phase_end(0x8FB70);xv_scene_phase_end(0x8FB70);
   assert(!object_sample_depth && !object_sample_active);
   assert(clock_calls-before==(selected?10u:0u));
  }
  xv_scene_phase_end(0x900E0);xv_scene_phase_end(0xFA920);
  assert(object_sample_seen==10000 && object_sample_chosen>80 && object_sample_chosen<240);
  assert(clock_calls==4+10*object_sample_chosen && !phase_depth[0] && phase_used[0]==7);
  for(unsigned i=0;i<5;i++)assert(phase_tab[0][i].n==object_sample_chosen);
  phase_report(60);assert(!object_sample_seen && !object_sample_chosen);
  puts("PASS sampled subtrees, recursive attribution, helper isolation and no unsampled clock reads");return 0;
 }
 if(mode==3) {
  xv_scene_phase_begin(0xFA920);
  for(unsigned i=0;i<10000;i++) {
   xv_scene_phase_begin(0x8FB70);xv_scene_phase_end(0x8FB70);
  }
  assert(clock_calls==1 && phase_depth[0]==1);
  helper=1;xv_scene_phase_begin(0x109760);xv_scene_phase_end(0x109760);
  assert(clock_calls==1 && !phase_depth[1]);helper=0;
  xv_scene_phase_begin(0x109760);xv_scene_phase_begin(0x900E0);
  xv_scene_phase_begin(0x8ECA0);
  for(unsigned i=0;i<10000;i++) {
   xv_scene_phase_begin(0x8FB70);xv_scene_phase_end(0x8FB70);
  }
  assert(clock_calls==4);
  xv_scene_phase_end(0x8ECA0);
  xv_scene_phase_end(0x900E0);xv_scene_phase_end(0x109760);
  xv_scene_phase_end(0xFA920);
  assert(clock_calls==8 && !phase_depth[0] && phase_used[0]==4);
  assert(phase_tab[0][0].addr==0x8ECA0 && phase_tab[0][0].parent==0x900E0);
  assert(phase_tab[0][1].addr==0x900E0 && phase_tab[0][1].parent==0x109760);
  assert(phase_tab[0][0].us==10 && phase_tab[0][1].us==30 && phase_tab[0][2].us==50 && phase_tab[0][3].us==70);
  /* Each synthetic pass occurs once per tick, as a sibling under 900E0. */
  xv_scene_phase_begin(0x900E0);
  for(uint32_t id=0xF0900001;id<=0xF0900003;id++) {
   xv_scene_phase_begin(id);
   xv_scene_phase_begin(0x8FB70);xv_scene_phase_end(0x8FB70);
   xv_scene_phase_end(id);
  }
  xv_scene_phase_end(0x900E0);
  assert(clock_calls==16 && !phase_depth[0] && phase_used[0]==8);
  for(unsigned i=4;i<7;i++) {
   assert(phase_tab[0][i].addr==0xF0900001+i-4);
   assert(phase_tab[0][i].parent==0x900E0 && phase_tab[0][i].us==10);
  }
  phase_report(60);puts("PASS sparse owner/lifecycle/pass scopes ignore callbacks and helper");return 0;
 }
 xv_scene_phase_begin(1);
 helper=1; xv_scene_phase_begin(2);xv_scene_phase_end(2);
 helper=0; xv_scene_phase_begin(3);xv_scene_phase_end(3);xv_scene_phase_end(1);
 assert(!phase_depth[0] && !phase_depth[1]);
 if(!mode) {assert(!clock_calls && !phase_used[0] && !phase_used[1]);}
 else {
  assert(phase_used[0]==2 && phase_tab[0][0].parent==1 && phase_tab[0][0].addr==3);
  assert(phase_tab[0][0].us==10 && phase_tab[0][1].addr==1);
  assert(phase_tab[0][1].us==(mode==1 ? 50 : 30));
  assert(clock_calls==(mode==1 ? 6 : 4));
  assert(phase_used[1]==(mode==1 ? 1 : 0));
  if(mode==1) assert(phase_tab[1][0].parent==0 && phase_tab[1][0].us==10);
 }
 phase_report(60);
 /* The passive selector must not mutate the policy read by root-pair math. */
 assert(atoi(getenv("XV_SCENE_PHASES"))==atoi(argv[1]));
 puts("PASS off/both/tick-only scope isolation and nested timing");
}
'''.replace('@BODY@',s[a:b])
if os.getenv('XITA_TEST_EMIT'):Path(os.environ['XITA_TEST_EMIT']).write_text(fixture)
with tempfile.TemporaryDirectory(prefix='xita-phase-mode-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture)
 subprocess.run(['cc','-O2','-fsanitize=address,undefined','-no-pie',str(p/'test.c'),'-o',str(p/'test')],check=True)
 for mode,tick in (('0','0'),('1','0'),('2','0'),('0','1'),('1','1'),('2','1'),('0','-1'),('0','2'),('0','3'),('1','2'),('2','2'),('3','2')):
  subprocess.run([str(p/'test'),mode,tick],check=True)
