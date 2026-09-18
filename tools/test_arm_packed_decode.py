#!/usr/bin/env python3
"""Execute synthetic packed SSE lowering on Cortex-A9; no game assets required."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
    UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR)
ROOT = Path(__file__).resolve().parents[1]; sys.path.insert(0, str(ROOT))
from tools.test_sse_packed_decode import generate
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--sdk', type=Path, default=Path('/home/birchwoodgod/vitasdk'))
args = parser.parse_args(); out = args.output_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
generate(out)
(out/'arm.c').write_text('''
#include <stddef.h>
#include "code_000.c"
uint8_t *g_xram, *g_img_base; uint32_t *g_xpt;
const unsigned layout[] = {sizeof(xctx), offsetof(xctx,r), offsetof(xctx,mm), offsetof(xctx,xmm)};
void x_guest_read_pages(void *dst, uint32_t a, size_t n) {
 for(size_t i=0;i<n;++i) ((uint8_t *)dst)[i]=g_xram[g_xpt[(a+i)>>12]+((a+i)&4095)]; }
void x_guest_write_pages(uint32_t a, const void *src, size_t n) {
 for(size_t i=0;i<n;++i) g_xram[g_xpt[(a+i)>>12]+((a+i)&4095)]=((const uint8_t *)src)[i]; }
void *memcpy(void *d,const void *s,size_t n) {for(size_t i=0;i<n;++i)((char *)d)[i]=((const char *)s)[i];return d;}
''')
elf_path = out/'packed.elf'
subprocess.run([str(args.sdk/'bin/arm-vita-eabi-gcc'), '-O2', '-mthumb', '-mcpu=cortex-a9',
 '-mfpu=neon', '-frounding-math', '-ffp-contract=off', '-fno-builtin', '-nostdlib',
 '-I'+str(ROOT/'recomp'), '-I'+str(out), str(out/'arm.c'), '-Wl,-Ttext=0x10000,-e,f_00011000',
 '-lgcc', '-o', str(elf_path)], check=True)
uc=Uc(UC_ARCH_ARM,UC_MODE_ARM); uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
uc.reg_write(UC_ARM_REG_C1_C0_2,15<<20);uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
with elf_path.open('rb') as file:
 elf=ELFFile(file); symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
 segments=[s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
 lo=min(s['p_vaddr'] for s in segments)&~4095;hi=max(s['p_vaddr']+s['p_memsz'] for s in segments)
 uc.mem_map(lo,(hi-lo+4095)&~4095)
 for s in segments:uc.mem_write(s['p_vaddr'],s.data())
size,r_off,mm_off,xmm_off=struct.unpack('<4I',uc.mem_read(symbols['layout'],16))
RAM,PT,CTX,STACK,END=0x20000000,0x21000000,0x22000000,0x23000000,0x24000000
for base,n in ((RAM,65536),(PT,4096),(CTX,4096),(STACK,65536),(END,4096)):uc.mem_map(base,n)
pages=[(i^1)*4096 for i in range(16)];uc.mem_write(PT,struct.pack('<16I',*pages))
for name,value in [('g_xram',RAM),('g_img_base',RAM),('g_xpt',PT)]:uc.mem_write(symbols[name],struct.pack('<I',value))
def pack(value):return struct.pack('<I',value&0xffffffff)
def f32(bits):return struct.unpack('<f',pack(bits))[0]
def convert(value,mode):
 integer=value if value<0x80000000 else value-0x100000000
 bits=struct.unpack('<I',struct.pack('<f',float(integer)))[0];v=f32(bits)
 if mode==1 and v<integer:bits+=1 if integer>=0 else -1
 if mode==2 and v>integer:bits+=-1 if integer>=0 else 1
 if mode==3 and abs(v)>abs(integer):bits-=1
 return bits, int(f32(bits)!=integer)*16
patterns=[0,0x80000000,0x7f800000,0xff800000,0x7f800001,0xff800001,0x7fc12345,0xffc54321,
 1,0x007fffff,0x80000001,0x807fffff,0x00800000,0x7f7fffff,0x3f800000,0x40800000,
 0x41000000,0xfffffffe,0x01000001,0xfeffffff,0x7fffffff,32767,0xffff8000]
count=0;max_error=0.0
for n in range(144):
 is_convert=n<72;dst=(n%72)//9;src=n%9
 for seed in range(0,23,3):
  for mode in range(4):
   context=bytearray(b'\xa5'*size);struct.pack_into('<I',context,r_off+16,0x8000)
   address=0x2ffc if seed&1 else 0x2011;struct.pack_into('<I',context,r_off,address)
   regs=[[patterns[(r*4+l+seed)%23] for l in range(4)] for r in range(8)]
   integers=[(patterns[(r+seed)%23],patterns[(r+seed+9)%23]) for r in range(8)]
   for r in range(8):
    struct.pack_into('<4I',context,xmm_off+r*16,*regs[r]);struct.pack_into('<2I',context,mm_off+r*8,*integers[r])
   data=struct.pack('<2I',*integers[src%8]) if is_convert else struct.pack('<4I',*regs[src%8])
   memory=bytearray(b'\xdb'*65536)
   if src==8:
    for i,b in enumerate(data):memory[pages[(address+i)>>12]+((address+i)&4095)]=b
   uc.mem_write(RAM,bytes(memory));uc.mem_write(CTX,bytes(context))
   # H2 supports nearest controls, but also check other native rounding modes;
   # RSQRT must preserve DN/FZ and all status bits without using FP arithmetic.
   fpscr=0xA8000000|(mode<<22)|(seed&0x9f)
   if not is_convert:fpscr|=0x03000000
   uc.reg_write(UC_ARM_REG_FPSCR,fpscr);uc.reg_write(UC_ARM_REG_R0,CTX)
   uc.reg_write(UC_ARM_REG_SP,STACK+0xFFF0);uc.reg_write(UC_ARM_REG_LR,END)
   uc.emu_start(symbols[f'f_{0x11000+n*16:08X}'],END,count=10000)
   result=bytes(uc.mem_read(CTX,size));wanted=bytearray(context);struct.pack_into('<I',wanted,r_off+16,0x8004)
   if is_convert:
    converted=[convert(v,mode) for v in integers[src%8]]
    struct.pack_into('<2I',wanted,xmm_off+dst*16,*[v for v,_ in converted])
    expected_fpscr=fpscr|converted[0][1]|converted[1][1]
   else:
    output=struct.unpack_from('<4I',result,xmm_off+dst*16)
    for bits,value in zip(regs[src%8],output):
     e=(bits>>23)&255;f=bits&0x7fffff
     if not e:assert value==(bits&0x80000000)|0x7f800000
     elif e==255 and f:assert value==bits|0x400000
     elif bits>>31:assert value==0xffc00000
     elif e==255:assert value==0
     else:
      error=abs(f32(value)*math.sqrt(f32(bits))-1);max_error=max(max_error,error);assert error<=1.5/4096
    struct.pack_into('<4I',wanted,xmm_off+dst*16,*output);expected_fpscr=fpscr
   assert result==wanted,(n,seed,mode,'context')
   assert bytes(uc.mem_read(RAM,65536))==memory,(n,seed,mode,'memory')
   assert uc.reg_read(UC_ARM_REG_FPSCR)==expected_fpscr,(n,seed,mode,hex(uc.reg_read(UC_ARM_REG_FPSCR)),hex(expected_fpscr))
   count+=1
report={'cases':count,'elf_sha256':hashlib.sha256(elf_path.read_bytes()).hexdigest(),
 'maximum_sampled_relative_error':max_error,'context_bytes':size,'result':'pass'}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
