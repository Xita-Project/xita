#!/usr/bin/env python3
"""Supplemental exact-context/FPSCR arithmetic tests, not a whole-query oracle.

Nonfinite vertices can make the retained whole-query reference itself fault
later in its edge visitor. This isolated per-instruction reference pins its
two additions to the retained whole-query VFP operand order. An independent
C compilation is insufficient: GCC may commute operands and select another
NaN payload. The whole-query oracle remains unchanged.
"""
import argparse,json,struct,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT)]
from prototype_query_semantic_leaf import ORIGINAL,HEADER
from test_arm_model_palette import Machine,RAM,CTX,STACK,END
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPSCR

def main():
    if not __debug__:raise SystemExit('Refusing optimized Python')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();out=a.out.resolve()
    if out.is_relative_to(ROOT):p.error('output must be outside source worktree')
    out.mkdir(parents=True,exist_ok=False)
    body=ORIGINAL.replace('X_MF32(q0)=c->xmm[2][0];','')
    first='c->xmm[2][0]+=c->xmm[0][0];'
    assert body.count(first)==2
    body=body.replace(first,'''{ float r;
      __asm__ volatile("vadd.f32 %0, %1, %2" : "=t"(r) : "t"(c->xmm[0][0]), "t"(c->xmm[2][0]));
      c->xmm[2][0]=r; }''',1)
    body=body.replace(first,'''{ float r;
      __asm__ volatile("vadd.f32 %0, %1, %2" : "=t"(r) : "t"(c->xmm[2][0]), "t"(c->xmm[0][0]));
      c->xmm[2][0]=r; }''',1)
    fixture='''#include "xv_x86rt.h"
#include <stddef.h>
#include "'''+str(HEADER)+'''"
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void test_boot(void){}
void original(xctx *c){
'''+body+'''
}
void candidate(xctx *c){nq_semantic_vertex_distance(c);}
'''
    (out/'fixture.c').write_text(fixture)
    cc='/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc'
    command=[cc,'-O2','-fno-strict-aliasing','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-std=gnu11',
        '-I'+str(ROOT/'recomp'),str(out/'fixture.c'),'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot',
        '-o',str(out/'fixture.elf')]
    subprocess.run(command,check=True)
    (out/'command.json').write_text(json.dumps(command,indent=2)+'\n')
    with (out/'fixture.asm').open('w') as f:subprocess.run([cc.removesuffix('gcc')+'objdump','-d',str(out/'fixture.elf')],stdout=f,check=True)
    m=Machine(out/'fixture.elf')
    def run(name,context,fpscr):
        u=m.uc;u.mem_write(CTX,context);u.reg_write(UC_ARM_REG_R0,CTX)
        u.reg_write(UC_ARM_REG_SP,STACK+65024);u.reg_write(UC_ARM_REG_LR,END|1)
        u.reg_write(UC_ARM_REG_FPSCR,fpscr)
        m.instructions=m.copies=m.copy_bytes=m.yields=0
        u.emu_start(m.symbols[name]|1,END,count=10000)
        assert u.reg_read(UC_ARM_REG_PC)==END
        return bytes(u.mem_read(CTX,m.layout['size'])),u.reg_read(UC_ARM_REG_FPSCR)
    edges=[0,0x80000000,1,0x80000001,0x007fffff,0x00800000,0x80800000,
           0x3df12345,0xbe234567,0x3e456789,0x3f800000,0x7f7fffff,0xff7fffff,
           0x7f800000,0xff800000,0x7fc12345,0x7fc23456,0xffc34567,
           0x7f812345,0x7f823456,0xff834567]
    vectors=[]
    for i in range(len(edges)):
        vectors.append([edges[(i+k)%len(edges)] for k in (0,1,2,3,7,9,11,13)])
        vectors.append([edges[i],0,edges[(i+1)%len(edges)],edges[(i+2)%len(edges)],0,0,0,0])
    rows=[]
    for fpscr in (0,0x400000,0x800000,0xc00000,0x0300009f):
        for number,words in enumerate(vectors):
            c=bytearray((i*37+11)&255 for i in range(m.layout['size']))
            struct.pack_into('<8I',c,m.layout['xmm'],*words)
            expected=run('original',bytes(c),fpscr);actual=run('candidate',bytes(c),fpscr)
            checks=dict(context=actual[0]==expected[0],fpscr=actual[1]==expected[1])
            row=dict(number=number,words=words,input_fpscr=fpscr,reference_fpscr=expected[1],candidate_fpscr=actual[1],checks=checks)
            if not all(checks.values()):
                row.update(reference_context=expected[0].hex(),candidate_context=actual[0].hex())
                (out/'failure.json').write_text(json.dumps(row,indent=2)+'\n');raise AssertionError(row)
            rows.append(row)
    # Probe exception-enable readback separately. Unicorn may clear these bits;
    # that cannot count as fallback or Vita exception-handler validation.
    enable_probes=[]
    for fpscr in (0x100,0x200,0x400,0x800,0x1000,0x8000,0x9f00):
        c=bytearray([0xa5]*m.layout['size'])
        struct.pack_into('<8I',c,m.layout['xmm'],0x3f800000,0,0x40000000,0x40800000,0,0,0,0)
        expected=run('original',bytes(c),fpscr);actual=run('candidate',bytes(c),fpscr)
        assert actual==expected
        enable_probes.append(dict(input_fpscr=fpscr,observed_fpscr=actual[1],enable_bits_preserved=(expected[1]&0x9f00)==fpscr,context_and_fpscr_equal=True))
    (out/'result.json').write_text(json.dumps(dict(cases=len(rows),rows=rows,native_exception_enable_probes=enable_probes,
        qualification='Supplemental per-instruction arithmetic with retained VFP operand order; not complete-query coverage'),indent=2)+'\n')
    print('PASS supplemental arithmetic reconstruction',len(rows),'complete contexts/FPSCR')

if __name__=='__main__':main()
