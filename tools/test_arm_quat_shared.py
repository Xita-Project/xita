#!/usr/bin/env python3
"""Vita-compiled optimistic quaternion transaction; locks are fixture stubs.

Worker ownership/concurrency is covered by test_object_private_math.py. This
checks production capture/validation/publication and FPSCR, not hardware FPS.
"""
import argparse,json,struct,subprocess
from pathlib import Path
import test_arm_quat_cache as base

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reference',type=Path,required=True)
p.add_argument('--output-dir',type=Path,required=True)
p.add_argument('--cc',default='arm-vita-eabi-gcc')
a=p.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
# Reuse the established independent lift/native harness and memory model.
elf,commands=base.build(a.output_dir,a.reference,a.cc)
h=a.output_dir/'arm-fixture.c'
s=h.read_text()+'''
unsigned hits,misses,unsupported,force_mutation;
static unsigned depth;
int xv_object_math_lock(void) {depth++;return 2;}
void xv_object_math_unlock(int *g) {if(*g)depth--;}
int xv_object_math_release_private(xctx *c,int *g,unsigned k,unsigned o,unsigned n,unsigned s,unsigned z)
{(void)c;(void)g;(void)k;(void)o;(void)n;(void)s;(void)z;return 0;}
int xv_object_quat_shared_admit(xctx *c,int g) {(void)c;return g==2&&depth==1;}
void xv_quat_shared_test_released(xctx *c) {
    if(force_mutation==1)X_M32(c->r[1])^=0x00100000u;
    if(force_mutation==2)g_xpt[c->r[1]>>12]^=0x1000u;
    if(force_mutation==3)g_xpt[c->r[2]>>12]^=0x1000u;
    if(force_mutation==4)X_M32(0x1F0A78u)^=0x00100000u;
}
void abort(void) {for(;;){}}
'''
h.write_text(s)
command=[x for x in commands[1] if x not in ('-DXV_QUAT_CACHE',str(base.ROOT/'recomp/kernel/xk_quat_cache.c'))]
command[1:1]=['-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_QUAT_SHARED_OUTPUT=1','-DXV_QUAT_SHARED_TEST',
 '-Wl,--undefined=hits,--undefined=misses,--undefined=unsupported,--undefined=force_mutation']
subprocess.run(command,check=True)
m=base.Machine(elf);cases=0
for rounding in range(4):
 for mode in (0,1<<24,1<<25,(1<<24)|(1<<25)):
  for case in range(40):
   sample,addresses=base.fixture(m.layout,case,case%2,rounding,mode)
   memory,context,pages,fpscr=sample
   # Only unaliased, aligned fixtures mutate; the production guard declines
   # other layouts before the transaction hook can execute.
   mutate=1+case//10 if case%10==0 else 0
   expected_memory=bytearray(memory);expected_pages=list(pages)
   source=struct.unpack_from('<I',context,m.layout['r']+4)[0]
   output=struct.unpack_from('<I',context,m.layout['r']+8)[0]
   if mutate in (1,4):
    address=source if mutate==1 else 0x1f0a78
    off=pages[address>>12]+(address&4095)
    word=struct.unpack_from('<I',expected_memory,off)[0]^0x00100000
    struct.pack_into('<I',expected_memory,off,word)
   if mutate==2:expected_pages[source>>12]^=0x1000
   if mutate==3:expected_pages[output>>12]^=0x1000
   expected=m.run('current_wrapper',(bytes(expected_memory),context,expected_pages,fpscr))
   m.uc.mem_write(m.symbols['force_mutation'],struct.pack('<I',mutate))
   actual=m.run('candidate_wrapper',sample)
   assert bytes(m.uc.mem_read(base.arm.PT,4*len(pages)))==struct.pack('<'+'I'*len(pages),*expected_pages)
   for field in ('context','memory'):
    assert actual[field]==expected[field],(rounding,mode,case,field)
   assert actual['fpscr']&0xfffffff==expected['fpscr']&0xfffffff,(rounding,mode,case,'fpscr')
   cases+=1
 def counter(name):return struct.unpack('<I',m.uc.mem_read(m.symbols[name],4))[0]
 print('PASS ARM rounding',rounding,'cases',cases,flush=True)
counts={k:counter('quat_shared_'+k) for k in ('attempts','commits','retries')}
assert counts['commits']>0 and counts['retries']>0 and counts['attempts']==counts['commits']+counts['retries'],counts
(a.output_dir/'result.json').write_text(json.dumps(dict(cases=cases,counts=counts,command=command),indent=2)+'\n')
print('PASS',cases,'ARM full context/memory/FPSCR comparisons;',counts)
