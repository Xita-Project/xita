#!/usr/bin/env python3
"""Compare actual Vita-linked bounds routines in Cortex-A9 instruction emulation.

Requires unicorn and pyelftools. Counts instructions, not CPU cycles or FPS.
Both executables are local inputs. getenv is intercepted (no settings); imported kernel memcpy/memmove perform
the requested byte copies. Their internal instructions are not simulated.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import random
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.arm_const import *

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--candidate', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
out = args.output_dir.resolve(); out.mkdir(parents=True, exist_ok=True)
fields = ['r','st','fsp','fsw','fcw','preempt','f_kind','f_bits']
meta = out/'layout.c'; obj = out/'layout.o'
meta.write_text('#include <stddef.h>\n#include "xv_x86rt.h"\nconst unsigned layout[] = {sizeof(xctx),'+','.join('offsetof(xctx,'+f+')' for f in fields)+'};\n')
subprocess.run(['arm-vita-eabi-gcc','-O2','-I'+str(root/'recomp'),'-c',str(meta),'-o',str(obj)],check=True)
with obj.open('rb') as f:
    elf=ELFFile(f);sym=elf.get_section_by_name('.symtab').get_symbol_by_name('layout')[0]
    data=elf.get_section(sym['st_shndx']).data()[sym['st_value']:sym['st_value']+sym['st_size']]
    layout=dict(zip(['size']+fields,struct.unpack('<'+'I'*(len(fields)+1),data)))

RAM=0x20000000; PT=0x21000000; STACK=0x22000000; CTX=0x23000000; END=0x24000000
SIZE=2<<20
pages=[(i^1)*4096 for i in range(SIZE//4096)]
pages[0x38]=pages[0x18];pages[0x39]=pages[0x19]
pt=struct.pack('<'+'I'*len(pages),*pages)


class Machine:
    def __init__(self,path):
        self.uc=uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        uc.reg_write(UC_ARM_REG_C1_C0_2,15<<20);uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
        with path.open('rb') as f:
            elf=ELFFile(f)
            self.symbols={s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
            for segment in elf.iter_segments():
                if segment['p_type']!='PT_LOAD':continue
                base=segment['p_vaddr'];start=base&~4095
                uc.mem_map(start,((base+segment['p_memsz']+4095)&~4095)-start)
                uc.mem_write(base,segment.data())
        for base,size in [(RAM,SIZE),(PT,4<<20),(STACK,65536),(CTX,4096),(END,4096)]:uc.mem_map(base,size)
        uc.mem_write(PT,pt)
        for name,value in [('g_xram',RAM),('g_xpt',PT),('g_img_base',RAM)]:
            uc.mem_write(self.symbols[name],struct.pack('<I',value))
        self.count=0;self.copy_calls=0;self.copy_bytes=0
        def step(uc,address,size,user):
            self.count+=1
            if address==(self.symbols['getenv']&~1):
                uc.reg_write(UC_ARM_REG_R0,0)
                uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
            for name in ('sceClibMemcpy','sceClibMemmove'):
                if address==(self.symbols[name]&~1):
                    dst=uc.reg_read(UC_ARM_REG_R0);src=uc.reg_read(UC_ARM_REG_R1);n=uc.reg_read(UC_ARM_REG_R2)
                    assert n<=SIZE
                    if n:uc.mem_write(dst,bytes(uc.mem_read(src,n)))
                    self.copy_calls+=1;self.copy_bytes+=n
                    uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
            assert address!=(self.symbols['xv_preempt']&~1),'unexpected preemption'
        uc.hook_add(UC_HOOK_CODE,step)
        def invalid(uc,access,address,size,value,user):
            print(str(path),"invalid access",access,hex(address),size,"PC",hex(uc.reg_read(UC_ARM_REG_PC)),flush=True)
            return False
        uc.hook_add(UC_HOOK_MEM_INVALID,invalid)
    def call(self,name,arg):
        self.count=0;self.copy_calls=0;self.copy_bytes=0
        self.uc.reg_write(UC_ARM_REG_R0,arg)
        self.uc.reg_write(UC_ARM_REG_SP,STACK+65024)
        self.uc.reg_write(UC_ARM_REG_LR,END|1)
        self.uc.emu_start(self.symbols[name]|1,END,count=200000)
        assert self.uc.reg_read(UC_ARM_REG_PC)==END,(name,hex(self.uc.reg_read(UC_ARM_REG_PC)))
        return self.count


baseline=Machine(args.baseline);candidate=Machine(args.candidate)
candidate.call('xv_native_bounds_override',1)
rng=random.Random(0x5c300)


def fixture(k):
    memory=bytearray(b'\xa5'*SIZE)
    ctx=bytearray(layout['size'])
    def write(a,data):
        for i,v in enumerate(data):memory[pages[(a+i)>>12]+((a+i)&4095)]=v
    def fl(a,x):write(a,struct.pack('<f',x))
    def word(a,x):write(a,struct.pack('<I',x))
    offset=[0,1,2,3,4092,4093,4094,4095][k%8]
    f=0x18000+offset;box=0x28000+offset;sp=0x62004 if k%17==0 else 0x61800+k%4
    if k%23==0:box=0x38000+offset
    if k%29==0:box=sp-0x60
    for i,v in enumerate([1,0,0,1,-1,0,0,1,0,1,0,1,0,-1,0,1]):fl(f+0x78+i*4,v)
    for i,v in enumerate([-1,-1,-1,1,-1,1,1,1,-1,-1,1,1,0,0,0]):fl(f+0xe0+i*4,v)
    for i in range(6):fl(f+0x128+i*4,3 if i&1 else -3);fl(box+i*4,.5 if i&1 else -.5)
    shape=(k//8)%8
    if shape==1:fl(box,4);fl(box+4,5)
    if shape==2:fl(box,1.5);fl(box+4,2)
    if shape in (3,4,5):fl(box,.5);fl(box+4,1.5)
    if shape==5:
        for i in range(5):fl(f+0xe0+i*12,-1)
    if shape==6:
        for i in range(16):fl(f+0x78+i*4,rng.randrange(-64,65)/16)
    if shape==7:
        edges=[0,0x80000000,1,0x807fffff,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f801234]
        for i in range(16):word(f+0x78+i*4,edges[(k+i)%len(edges)])
    for i in range(8):struct.pack_into('<I',ctx,i*4,rng.getrandbits(32));struct.pack_into('<d',ctx,layout['st']+i*8,i+.125)
    struct.pack_into('<I',ctx,4,f);struct.pack_into('<I',ctx,7*4,box);struct.pack_into('<I',ctx,4*4,sp)
    struct.pack_into('<I',ctx,layout['fsp'],k%8);struct.pack_into('<H',ctx,layout['fsw'],rng.randrange(65536))
    struct.pack_into('<H',ctx,layout['fcw'],0x37f)
    struct.pack_into('<I',ctx,layout['preempt'],1000000)
    struct.pack_into('<I',ctx,layout['f_kind'],3);struct.pack_into('<I',ctx,layout['f_bits'],32)
    fl(0x1f0a68,0);word(sp,0x12345678);word(sp+4,0 if shape==3 else 1)
    return memory,ctx,shape,sp


rows=[];outcomes={0:0,1:0,2:0}
for k in range(256):
    memory,ctx,shape,sp=fixture(k)
    results=[]
    for machine in [baseline,candidate]:
        machine.uc.mem_write(RAM,bytes(memory));machine.uc.mem_write(CTX,bytes(ctx))
        machine.uc.reg_write(UC_ARM_REG_FPSCR,(k//64)<<22)
        n=machine.call('f_0005C300',CTX)
        results.append([bytearray(machine.uc.mem_read(CTX,layout['size'])),bytearray(machine.uc.mem_read(RAM,SIZE)),n,machine.copy_calls,machine.copy_bytes])
    a,b=results
    for i in range(8):
        offset=layout['st']+i*8
        if math.isnan(struct.unpack_from('<d',a[0],offset)[0]) and math.isnan(struct.unpack_from('<d',b[0],offset)[0]):a[0][offset:offset+8]=b[0][offset:offset+8]=bytes(8)
    assert a[0]==b[0],('context',k,[(i,x,y) for i,(x,y) in enumerate(zip(a[0],b[0])) if x!=y])
    if a[1]!=b[1]:
        for i in range(0,96,4):
            addrs=[pages[(sp-96+i+j)>>12]+((sp-96+i+j)&4095) for j in range(4)]
            x=struct.unpack('<f',bytes(a[1][p] for p in addrs))[0];y=struct.unpack('<f',bytes(b[1][p] for p in addrs))[0]
            if math.isnan(x) and math.isnan(y):
                for p in addrs:b[1][p]=a[1][p]
        assert a[1]==b[1],('memory',k)
    result=struct.unpack_from('<H',a[0])[0];outcomes[result]+=1
    rows.append({'case':k,'shape':shape,'outcome':result,'original_instructions':a[2],'candidate_instructions':b[2],'original_kernel_copy_calls':a[3],'candidate_kernel_copy_calls':b[3],'original_kernel_copy_bytes':a[4],'candidate_kernel_copy_bytes':b[4]})
assert all(outcomes.values())
report={'cases':len(rows),'outcomes':outcomes,'layout':layout,'instruction_counts_not_cycles':rows,
        'baseline_sha256':hashlib.sha256(args.baseline.read_bytes()).hexdigest(),
        'candidate_sha256':hashlib.sha256(args.candidate.read_bytes()).hexdigest()}
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS:',len(rows),'actual Vita-compiled entry comparisons, complete context/memory, all alignments and rounding modes')
for shape in range(8):
    group=[r for r in rows if r['shape']==shape]
    x=sum(r['original_instructions'] for r in group)/len(group);y=sum(r['candidate_instructions'] for r in group)/len(group)
    print('Shape',shape,'mean instructions',round(x,1),'->',round(y,1),'(not cycles or FPS)')
