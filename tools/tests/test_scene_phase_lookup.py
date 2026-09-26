"""Compare production lookup hints with linear lookup through collisions/reordering."""
from pathlib import Path
import argparse
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
s=(root/'recomp/kernel/xk_scene_thread.c').read_text()
s=s[s.index('#define PHASE_MAX'):s.index('static void phase_report(unsigned frames)')]
a=s.index('    unsigned h =');b=s.index('    phase_tab[t][i].us += dt;',a)
linear=s[:a]+'''    for(i=0;i<phase_used[t];i++)if(phase_tab[t][i].addr==addr && phase_tab[t][i].parent==parent)break;
    if(i==phase_used[t]) {if(i==PHASE_MAX)return;phase_tab[t][i].parent=parent;phase_tab[t][i].addr=addr;phase_used[t]++;}
'''+s[b:]
pre='''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
static unsigned helper;
static uint64_t ticks;
int xv_scene_thread_on_helper(void) { return helper; }
uint64_t xk_os_monotonic_us(void) { return ++ticks; }
'''
post='''
int main(void) {
 phases=1;uint32_t seed=12345;struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
 for(unsigned k=0;k<1000000;k++) {
  seed=seed*1664525u+1013904223u;helper=(seed>>31)&1;
  unsigned j=(seed>>8)%400;
  xv_scene_phase_begin(0x70000+(j%4)*16);
  xv_scene_phase_begin(0xA0000+j*16);
  xv_scene_phase_end(0xA0000+j*16);
  xv_scene_phase_end(0x70000+(j%4)*16);
  /* Reordering invalidates old index hints, just as report sorting does. */
  if(k%997==0 && phase_used[helper]>1) {
   unsigned x=(seed>>16)%phase_used[helper];
   typeof(phase_tab[0][0]) tmp=phase_tab[helper][0];
   phase_tab[helper][0]=phase_tab[helper][x];phase_tab[helper][x]=tmp;
  }
 }
 clock_gettime(CLOCK_MONOTONIC,&b);
 uint64_t digest=0;
 for(unsigned t=0;t<2;t++)for(unsigned i=0;i<phase_used[t];i++) {
  uint64_t key=((uint64_t)phase_tab[t][i].parent<<32)|phase_tab[t][i].addr;
  digest+=key*(phase_tab[t][i].us+17*phase_tab[t][i].n);
 }
 printf("%llu %u %u\\n",(unsigned long long)digest,phase_used[0],phase_used[1]);
 fprintf(stderr,"elapsed-ms %.3f\\n",(b.tv_sec-a.tv_sec)*1000.0+(b.tv_nsec-a.tv_nsec)/1e6);
}
'''
ap=argparse.ArgumentParser();ap.add_argument('--cc',default='cc');ap.add_argument('--keep',type=Path);args=ap.parse_args()
with tempfile.TemporaryDirectory() as tmp:
 p=args.keep or Path(tmp);p.mkdir(exist_ok=True,parents=True);out=[]
 for name,src in [('linear',linear),('hint',s)]:
  (p/(name+'.c')).write_text(pre+src+post)
  subprocess.run([args.cc,'-O2','-std=gnu11','-static',str(p/(name+'.c')),'-o',str(p/name)],check=True)
  if not args.keep:
   r=subprocess.run([str(p/name)],check=True,capture_output=True,text=True);out.append(r.stdout);print(name,r.stderr.strip(),r.stdout.strip())
 if not args.keep:
  assert out[0]==out[1],out
  print('PASS: production and reference totals match after collisions and reordering')
