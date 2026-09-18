#!/usr/bin/env python3
"""Compare emitted MINPS/MAXPS on Cortex-A9 with independent native x86 SSE."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
    UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR)
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from tools.test_sse_minmax import generate
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir',type=Path,required=True)
parser.add_argument('--sdk',type=Path,default=Path('/home/birchwoodgod/vitasdk'))
args=parser.parse_args();out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
generate(out)
(out/'arm.c').write_text('''
#include <stddef.h>
#include "code_000.c"
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt; uint32_t fault_ip;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,xmm)};
void xv_unimpl(xctx *c,uint32_t ip,const char *what){(void)c;(void)what;fault_ip=ip;}
void x_guest_read_pages(void *dst,uint32_t a,size_t n){
 for(size_t i=0;i<n;++i)((uint8_t *)dst)[i]=g_xram[g_xpt[(a+i)>>12]+((a+i)&4095)];}
void x_guest_write_pages(uint32_t a,const void *src,size_t n){
 for(size_t i=0;i<n;++i)g_xram[g_xpt[(a+i)>>12]+((a+i)&4095)]=((const uint8_t *)src)[i];}
void *memcpy(void *d,const void *s,size_t n){for(size_t i=0;i<n;++i)((char *)d)[i]=((const char *)s)[i];return d;}
''')
elf_path=out/'minmax.elf'
subprocess.run([str(args.sdk/'bin/arm-vita-eabi-gcc'),'-O2','-mthumb','-mcpu=cortex-a9',
 '-mfpu=neon','-fno-builtin','-nostdlib','-I'+str(ROOT/'recomp'),'-I'+str(out),str(out/'arm.c'),
 '-Wl,-Ttext=0x10000,-e,f_00011000','-lgcc','-o',str(elf_path)],check=True)
oracle_path=out/'oracle.so'
subprocess.run(['cc','-O2','-shared','-fPIC','-DXITA_MINMAX_ORACLE_ONLY',
 str(ROOT/'tools/test_sse_minmax.c'),'-o',str(oracle_path)],check=True)
oracle=ctypes.CDLL(str(oracle_path)).minmax_oracle
word=ctypes.c_uint32;vector=word*4
oracle.argtypes=[ctypes.POINTER(word),ctypes.POINTER(word),ctypes.POINTER(word),word,ctypes.POINTER(word)]
uc=Uc(UC_ARCH_ARM,UC_MODE_ARM);uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
uc.reg_write(UC_ARM_REG_C1_C0_2,15<<20);uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
with elf_path.open('rb') as file:
 elf=ELFFile(file);symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
 segments=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
 lo=min(s['p_vaddr'] for s in segments)&~4095;hi=max(s['p_vaddr']+s['p_memsz'] for s in segments)
 uc.mem_map(lo,(hi-lo+4095)&~4095)
 for s in segments:uc.mem_write(s['p_vaddr'],s.data())
size,r_off,xmm_off=struct.unpack('<3I',uc.mem_read(symbols['layout'],12))
RAM,PT,CTX,STACK,END=0x20000000,0x21000000,0x22000000,0x23000000,0x24000000
for base,n in ((RAM,65536),(PT,4096),(CTX,4096),(STACK,65536),(END,4096)):uc.mem_map(base,n)
pages=[(i^1)*4096 for i in range(16)];uc.mem_write(PT,struct.pack('<16I',*pages))
for name,value in [('g_xram',RAM),('g_img_base',RAM),('g_xpt',PT)]:uc.mem_write(symbols[name],struct.pack('<I',value))
patterns=[0,0x80000000,0x7f800000,0xff800000,0x7f800001,0xff800001,0x7fc12345,0xffc54321,
 1,0x007fffff,0x80000001,0x807fffff,0x00800000,0x7f7fffff,0x3f800000,0xbf800000,
 0x40800000,0xc0800000,0x01000001,0x81000001,0x7fffffff,0xffffffff,0x3f800001,0xbf800001,
 0x00800001,0x80800001,0x7fc00000,0xffc00000,0x00400000,0x80400000,0x7f800002,0xff800002]
def execute(index,context,memory,fpscr):
 uc.mem_write(RAM,bytes(memory));uc.mem_write(CTX,bytes(context));uc.mem_write(symbols['fault_ip'],b'\0'*4)
 uc.reg_write(UC_ARM_REG_FPSCR,fpscr);uc.reg_write(UC_ARM_REG_R0,CTX)
 uc.reg_write(UC_ARM_REG_SP,STACK+0xfff0);uc.reg_write(UC_ARM_REG_LR,END)
 uc.emu_start(symbols[f'f_{0x11000+index*16:08X}'],END,count=10000)
 return bytes(uc.mem_read(CTX,size)),uc.reg_read(UC_ARM_REG_FPSCR),struct.unpack('<I',uc.mem_read(symbols['fault_ip'],4))[0]
count=0;rejected=0;unwritable=0
for n in range(144):
 maximum=n>=72;dst=(n%72)//9;src=n%9
 for seed in range(32):
  context=bytearray(b'\xa5'*size);struct.pack_into('<I',context,r_off+16,0x8000)
  address=0x2ffc if seed&1 else 0x2011;struct.pack_into('<I',context,r_off,address)
  regs=[[patterns[(r*5+l+seed)%32] for l in range(4)] for r in range(8)]
  for r in range(8):struct.pack_into('<4I',context,xmm_off+r*16,*regs[r])
  memory=bytearray(b'\xdb'*65536);data=struct.pack('<4I',*regs[src%8])
  if src==8:
   for i,b in enumerate(data):memory[pages[(address+i)>>12]+((address+i)&4095)]=b
  for mode in range(4):
   fpscr=0xa8000000|(mode<<22)|((seed*13)&0x9f)
   csr=word(0x1f80|([0,2,1,3][mode]<<13)|(fpscr&1)|((fpscr>>6)&2)|((fpscr&0x1e)<<1))
   expected=vector();oracle(expected,vector(*regs[dst]),vector(*regs[src%8]),maximum,ctypes.byref(csr))
   expected_fpscr=(fpscr&~0x9f)|(csr.value&1)|((csr.value&2)<<6)|((csr.value>>1)&0x1e)
   wanted=bytearray(context);struct.pack_into('<I',wanted,r_off+16,0x8004)
   struct.pack_into('<4I',wanted,xmm_off+dst*16,*expected)
   result,after,fault=execute(n,context,memory,fpscr)
   assert result==wanted,(n,seed,mode,'context')
   assert after==expected_fpscr,(n,seed,mode,hex(after),hex(expected_fpscr))
   assert not fault,(n,seed,mode,'unexpected strict stop')
   assert bytes(uc.mem_read(RAM,65536))==memory,(n,seed,mode,'memory');count+=1
  if seed==0:
   for bit in (8,9,10,11,12,15,16,17,18,20,21,24,25):
    fpscr=0xa800009f|(1<<bit)
    uc.reg_write(UC_ARM_REG_FPSCR,fpscr)
    applied=uc.reg_read(UC_ARM_REG_FPSCR)
    if applied!=fpscr:
     # Cortex-A9/Unicorn exposes some exception enables as read-as-zero.
     # Do not count a control the emulated CPU cannot set as a rejection.
     assert applied==(fpscr&~(1<<bit)),(bit,hex(applied))
     unwritable+=1;continue
    result,after,fault=execute(n,context,memory,fpscr)
    assert result==context and after==fpscr,(n,bit,'rejection mutated state')
    assert fault==0x11000+n*16,(n,bit,'missing strict stop')
    assert bytes(uc.mem_read(RAM,65536))==memory,(n,bit,'rejection mutated memory');rejected+=1
report={'native_oracle_comparisons':count,'unsupported_control_rejections':rejected,
 'unwritable_control_probes_excluded':unwritable,
 'elf_sha256':hashlib.sha256(elf_path.read_bytes()).hexdigest(),'context_bytes':size,'result':'pass'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
