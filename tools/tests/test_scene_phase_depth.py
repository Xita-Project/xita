"""Overflowing nested diagnostics must not consume an outer call's timer."""
from pathlib import Path
import argparse
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, default=root / 'recomp/kernel/xk_scene_thread.c')
source = parser.parse_args().source.read_text()
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
 const unsigned capacity=sizeof(phase_addr[0])/sizeof(phase_addr[0][0]);
 phases=1;
 for (unsigned t=0;t<2;t++) {
  helper=t;
  for(unsigned i=0;i<capacity+4;i++)xv_scene_phase_begin(100+i);
  assert(phase_depth[t]==capacity && phase_skipped[t]==4);
 }
 /* Interleaved owner/helper scopes keep independent stacks. */
 for(unsigned i=capacity+4;i>0;i--)for(unsigned t=0;t<2;t++) {
  helper=t;xv_scene_phase_end(100+i-1);
 }
 for(unsigned t=0;t<2;t++) {
  assert(!phase_depth[t] && !phase_skipped[t] && phase_overflow[t]==4);
  assert(phase_used[t]==capacity);
  for(unsigned i=0;i<capacity;i++) {
   assert(phase_tab[t][i].addr==100+capacity-1-i);
   assert(phase_tab[t][i].parent==(i==capacity-1?0:100+capacity-2-i));
   assert(phase_tab[t][i].n==1 && phase_tab[t][i].us>0);
  }
  helper=t;xv_scene_phase_begin(200);xv_scene_phase_end(200);
  assert(phase_tab[t][capacity].addr==200 && phase_tab[t][capacity].parent==0);
 }
 puts("PASS: overflow recovery, outer attribution and per-thread isolation");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture)
 subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Wno-unused-variable','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
