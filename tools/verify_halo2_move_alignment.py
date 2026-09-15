"""Private owned-XBE copy-path oracle; emits no distributable game artifacts.

Run with an owned supported XBE and an empty output path outside this checkout.
Requires iced_x86, Unicorn and a host C compiler. Arithmetic flags are volatile
at the CRT call boundary; compare all GPRs/ESP, DF and the complete memory arena.
"""
from pathlib import Path
import argparse
import sys,json,ctypes,struct,subprocess,hashlib
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from games.halo2_5849.prepare_boot import game_move_alignment_roots
from unicorn import Uc,UC_ARCH_X86,UC_MODE_32
from unicorn.x86_const import *
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('xbe',type=Path)
parser.add_argument('--out',required=True,type=Path)
args=parser.parse_args()
p=args.out.resolve()
if p.is_relative_to(ROOT):
    parser.error('owned generated code must stay outside the source checkout')
p.mkdir(parents=True,exist_ok=False)
out=p/'oracle-generated';out.mkdir()
im=r.Image(str(args.xbe))
from recompiler.core.profile import load_profile
load_profile('halo2_5849').validate_image(im)
roots=game_move_alignment_roots(im)|{0x320890};d=r.Discovery(im,{},{},lambda *_:None)
for addr in sorted(roots):d.add_root(addr)
d.run();e=r.Emitter(im,d,{},{},str(out),1);e.write_all();assert not e.unimpl
assert set(d.functions)==roots,set(d.functions)
code=im.bytes_at(0x320000,0x1000);(p/'owned-code.bin').write_bytes(code)
h='''#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "oracle-generated/code_000.c"
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
void xv_preempt(xctx *c){(void)c;}
void xv_trap(xctx *c,uint32_t ip){(void)c;fprintf(stderr,"unexpected trap %08X\\n",ip);abort();}
void xv_call(xctx *c,uint32_t target){switch(target){'''
for addr in sorted(roots):h+=f'case 0x{addr:X}:f_{addr:08X}(c);return;'
h+='''default:xv_trap(c,target);}}
void initialize(const uint8_t *code){g_xram=calloc(1,0x400000);g_xpt=calloc(1u<<20,4);assert(g_xram&&g_xpt);g_img_base=g_xram+0x10000;for(unsigned i=0;i<1024;++i)g_xpt[i]=i*4096;memcpy(g_xram+0x320000,code,4096);}
void run_case(const uint8_t *input,const uint32_t *registers,uint32_t *output,uint8_t *memory){
 memcpy(g_xram+0x10000,input,65536);xctx cpu={0};memcpy(cpu.r,registers,32);xf_set_eflags(&cpu,0x202);f_00320890(&cpu);memcpy(output,cpu.r,32);output[8]=xf_eflags(&cpu);memcpy(memory,g_xram+0x10000,65536);
}
''';h='#include <stdio.h>\n'+h;(p/'oracle.c').write_text(h)
root=ROOT
(p/'oracle.exports').write_text('{ global: initialize; run_case; local: *; };\n')
subprocess.run(['cc','-c','-fPIC','-std=gnu11','-O2','-ffunction-sections','-fdata-sections','-fno-strict-aliasing','-Dxv_call=unused_runtime_call','-Dxv_trap=unused_runtime_trap','-Dxv_preempt=unused_runtime_preempt',str(root/'recomp/xv_x86rt.c'),'-o',str(p/'runtime.o')],check=True)
subprocess.run(['cc','-shared','-fPIC','-std=gnu11','-O2','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-I'+str(root/'recomp'),str(p/'oracle.c'),str(p/'runtime.o'),'-Wl,--gc-sections,--version-script='+str(p/'oracle.exports'),'-o',str(p/'oracle.so')],check=True)

lib=ctypes.CDLL(str(p/'oracle.so'));lib.initialize.argtypes=[ctypes.c_void_p];lib.initialize(code)
lib.run_case.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p]
u=Uc(UC_ARCH_X86,UC_MODE_32);u.mem_map(0x10000,65536);u.mem_map(0x320000,4096);u.mem_write(0x320000,code);u.mem_map(0x380000,4096)
regs=[UC_X86_REG_EAX,UC_X86_REG_ECX,UC_X86_REG_EDX,UC_X86_REG_EBX,UC_X86_REG_ESP,UC_X86_REG_EBP,UC_X86_REG_ESI,UC_X86_REG_EDI]
base=bytes((i*71^(i>>4))&255 for i in range(65536));output=(ctypes.c_uint32*9)();memory=(ctypes.c_ubyte*65536)();cases=0
for n in list(range(0,132))+[255,256,257,511,512,513,1023,1024,1025,2047,2048,2049]:
 for srcalign in range(4):
  source=0x14000+srcalign
  destinations=[0x11000+k for k in range(4)]+[0x17000+k for k in range(4)]+[source+k for k in [-65,-33,-9,-4,-3,-2,-1,0,1,2,3,4,9,33,65]]
  for destination in destinations:
   initial=bytearray(base);struct.pack_into('<4I',initial,0x8000,0x380000,destination,source,n)
   initial=bytes(initial);registers=[0xAA112233,0xBB445566,0xCC778899,0xDD001122,0x18000,0xEE334455,0xFF667788,0x1199AABB]
   rv=(ctypes.c_uint32*8)(*registers);lib.run_case(initial,rv,output,memory)
   u.mem_write(0x10000,initial)
   for reg,value in zip(regs,registers):u.reg_write(reg,value)
   u.reg_write(UC_X86_REG_EFLAGS,0x202);u.emu_start(0x320890,0x380000,count=20000)
   assert u.reg_read(UC_X86_REG_EIP)==0x380000,(n,source,destination)
   expected=[u.reg_read(reg)for reg in regs]
   assert list(output)[:8]==expected,(n,source,destination,list(output),expected)
   assert (output[8]&0x400)==(u.reg_read(UC_X86_REG_EFLAGS)&0x400)==0
   assert bytes(memory)==u.mem_read(0x10000,65536),(n,source,destination,'memory')
   # Independent overlap-safe expected copy; compare entire data arena below stack.
   expected_data=bytearray(initial);off=destination-0x10000;src=source-0x10000
   expected_data[off:off+n]=initial[src:src+n]
   assert bytes(memory)[:0x7FF0]==expected_data[:0x7FF0],(n,source,destination,'memmove')
   cases+=1
report=dict(cases=cases,scope='owned original memmove vs lifted code and independent overlap-safe bytes; all8GPRs/ESP/DF/full64KiB including stack; arithmetic flags not ABI comparison',roots=sorted(roots),xbe_sha256=hashlib.sha256(args.xbe.read_bytes()).hexdigest(),compiler='cc -O2 -fno-strict-aliasing',passed=True)
(p/'oracle-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
