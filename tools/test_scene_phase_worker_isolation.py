#!/usr/bin/env python3
"""Exercise production phase stacks while excluded worker threads run callbacks."""
from pathlib import Path
import subprocess
import tempfile
source = (Path(__file__).resolve().parents[1] / 'recomp/kernel/xk_scene_thread.c').read_text()
section = source[source.index('#define PHASE_MAX'):source.index('static void phase_report(unsigned frames)')]
prefix = r'''
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <time.h>
static _Thread_local int worker, scene;
int xv_object_is_worker_thread(void) { return worker; }
int xv_scene_thread_on_helper(void) { return scene; }
uint64_t xk_os_monotonic_us(void) {
 struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
 return (uint64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;
}
'''
suffix = r'''
static void *run_worker(void *unused) {
 (void)unused; worker=1;
 for(unsigned i=0;i<200000;i++) {
  xv_scene_phase_begin(0xBAD); xv_scene_phase_begin(0xBAD1);
  xv_scene_phase_end(0xBAD1); xv_scene_phase_end(0xBAD);
 }
 return 0;
}
static void *run_scene(void *unused) {
 (void)unused; scene=1;
 for(unsigned i=0;i<10000;i++) {
  xv_scene_phase_begin(0x200); xv_scene_phase_end(0x200);
 }
 return 0;
}
int main(void) {
 setenv("XV_SCENE_PHASES","1",1);
 pthread_t a,b,c;
 xv_scene_phase_begin(0x100);
 assert(!pthread_create(&a,0,run_worker,0));
 assert(!pthread_create(&b,0,run_worker,0));
 assert(!pthread_create(&c,0,run_scene,0));
 for(unsigned i=0;i<10000;i++) {
  xv_scene_phase_begin(0x101); xv_scene_phase_end(0x101);
 }
 pthread_join(a,0); pthread_join(b,0); pthread_join(c,0);
 xv_scene_phase_end(0x100);
 assert(phase_used[0]==2 && phase_used[1]==1);
 assert(phase_tab[0][0].addr==0x101 && phase_tab[0][0].parent==0x100);
 assert(phase_tab[0][0].n==10000 && phase_tab[0][1].n==1);
 assert(phase_tab[1][0].addr==0x200 && phase_tab[1][0].n==10000);
 for(int i=0;i<2;i++) assert(!phase_depth[i] && !phase_skipped[i] && !phase_overflow[i]);
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d); (p/'test.c').write_text(prefix+section+suffix)
 subprocess.run(['cc','-std=gnu11','-O2','-pthread','-DXV_EXPERIMENTAL_OBJECT_JOBS=1',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
 # Regression sensitivity: dropping both worker guards must fail the assertions.
 (p/'bad.c').write_text((prefix+section+suffix).replace('if (xv_object_is_worker_thread()) return;', ''))
 subprocess.run(['cc','-std=gnu11','-O2','-pthread','-DXV_EXPERIMENTAL_OBJECT_JOBS=1',str(p/'bad.c'),'-o',str(p/'bad')],check=True)
 result=subprocess.run([str(p/'bad')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 assert result.returncode != 0, 'unprotected shared stack unexpectedly passed'
print('PASS owner/scene phase isolation with two concurrent workers; unprotected mutant fails')
