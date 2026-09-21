#!/usr/bin/env python3
"""Compile the profile's exact guard and check its scope and retained-shard patch."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from games.halo_ce_3925.loading_brightness import before_instruction, LINES
from tools.patch_loading_brightness import patch
assert before_instruction(0xD4D09,True)==LINES
assert before_instruction(0xD4D09,False)==[]
assert before_instruction(0x1D370,True)==[]
site='    /* 000D4D09  call 0001D370h */\n'
source='void f_000D4C40(xctx *restrict c)\n{\n'+site*2+'}\nvoid f_other(void) {}\n'
changed=patch(source)
assert patch(changed)==changed and changed.endswith('void f_other(void) {}\n')
try:patch(source.replace(site,'',1))
except ValueError:pass
else:raise AssertionError('partial call coverage accepted')
program=r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#define X_ST(i) st[i]
static void guard(double st[8]) {
GUARD
}
int main(void) {
 double cases[]={-.0066567957401275635,-.02003757283091545,-1,0,-0.,1e-30,.5,1,INFINITY,NAN};
 for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) {
  double st[8]={.8999999761581421,cases[i],3,4,5,6,7,8},before[8];
  memcpy(before,st,sizeof st);guard(st);
  assert(!memcmp(st,before,sizeof(double)));
  assert(!memcmp(st+2,before+2,6*sizeof(double)));
  if(cases[i]<0)assert(st[1]==0 && isfinite(pow(st[1],st[0])));
  else assert(!memcmp(st+1,before+1,sizeof(double)));
 }
}
'''.replace('GUARD','\n'.join(LINES))
with tempfile.TemporaryDirectory() as t:
 p=Path(t);(p/'test.c').write_text(program)
 subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS: loading-only scope, captured negative values, positive/nonfinite preservation, adjacent stack slots, idempotent complete patch')
