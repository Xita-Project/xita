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
 assert(argc==2);int mode=atoi(argv[1]);setenv("XV_SCENE_PHASES",argv[1],1);
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
 puts("PASS off/both/tick-only scope isolation and nested timing");
}
'''.replace('@BODY@',s[a:b])
if os.getenv('XITA_TEST_EMIT'):Path(os.environ['XITA_TEST_EMIT']).write_text(fixture)
with tempfile.TemporaryDirectory(prefix='xita-phase-mode-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture)
 subprocess.run(['cc','-O2','-fsanitize=address,undefined','-no-pie',str(p/'test.c'),'-o',str(p/'test')],check=True)
 for mode in ('0','1','2'):subprocess.run([str(p/'test'),mode],check=True)
