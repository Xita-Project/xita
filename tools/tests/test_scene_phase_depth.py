"""Overflowing nested diagnostics must not consume an outer call's timer."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'recomp/kernel/xk_scene_thread.c').read_text()
start = source.index('#define PHASE_MAX')
end = source.index('static void phase_report(unsigned frames)', start)
fixture = '''
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
static unsigned helper;
static uint64_t now;
int xv_scene_thread_on_helper(void) { return helper; }
uint64_t xk_os_monotonic_us(void) { return ++now; }
'''+source[start:end]+'''
int main(void) {
 phases=1;
 for (unsigned t=0;t<2;t++) {
  helper=t;
  for(unsigned i=0;i<20;i++)xv_scene_phase_begin(100+i);
  assert(phase_depth[t]==16 && phase_skipped[t]==4);
 }
 /* Interleaved owner/helper scopes keep independent stacks. */
 for(unsigned i=20;i>0;i--)for(unsigned t=0;t<2;t++) {
  helper=t;xv_scene_phase_end(100+i-1);
 }
 for(unsigned t=0;t<2;t++) {
  assert(!phase_depth[t] && !phase_skipped[t] && phase_overflow[t]==4);
  assert(phase_used[t]==16);
  for(unsigned i=0;i<16;i++) {
   assert(phase_tab[t][i].addr==115-i);
   assert(phase_tab[t][i].parent==(i==15?0:114-i));
   assert(phase_tab[t][i].n==1 && phase_tab[t][i].us>0);
  }
  helper=t;xv_scene_phase_begin(200);xv_scene_phase_end(200);
  assert(phase_tab[t][16].addr==200 && phase_tab[t][16].parent==0);
 }
 puts("PASS: overflow recovery, outer attribution and per-thread isolation");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture)
 subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Wno-unused-variable','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
