#!/usr/bin/env python3
"""Vita-compiled reference coverage/equality with strict ARM read/write bounds.

Requires VitaSDK, Unicorn (tested with 2.1.3) and pyelftools. Firmware copy/fill calls are modeled;
this checks correctness, not cache traffic, firmware behavior, cycles or FPS.
"""
import argparse
import json
import hashlib
from pathlib import Path
import random
import struct
import subprocess
import unicorn
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_MEM_READ, UC_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_CPU_ARM_CORTEX_A9,
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', type=Path, required=True)
out = parser.parse_args().output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
(out/'test.c').write_text('''#include "runtime/xv_index_copy.h"
#include "runtime/xv_bytes_equal.h"
unsigned test_capture(void *dst,const void *src,unsigned n,xv_vertex_refs *r)
{ return xv_index_copy_reference_bounds(dst,src,n,r); }
unsigned test_capture_neon(void *dst,const void *src,unsigned n,xv_vertex_refs *r)
{ return xv_index_copy_reference_bounds_neon(dst,src,n,r); }
static int equal(const void *a,const void *b,unsigned n) { return xv_bytes_equal(a,b,n); }
int test_equal(const void *a,const void *b,const xv_vertex_refs *r,unsigned stride)
{
    unsigned bytes=r->vertices*stride,runs=0;uint64_t checked=0;
    return xv_vertex_refs_sparse(r,bytes,stride) ?
        xv_vertex_refs_equal(r,stride,a,b,equal,&checked,&runs) : equal(a,b,bytes);
}
void *sceClibMemcpy(void *dst,const void *src,unsigned n) { return dst; }
void *sceClibMemset(void *dst,int ch,unsigned n) { return dst; }
/* A hooked conditional store must not predicate instructions after the IT
 * block. Include a branch boundary to exercise restored translation state. */
__attribute__((naked)) unsigned test_hooks(void *p)
{
    __asm__("movs r2,#1\\n cmp r2,#0\\n it hi\\n strhi r2,[r0]\\n"
            "cmp r2,#1\\n bne 1f\\n mov r0,r2\\n1: bx lr");
}
''')
binary = out/'test.elf'
subprocess.run(['arm-vita-eabi-gcc','-O2','-mthumb','-mcpu=cortex-a9','-mfpu=neon',
    '-nostdlib','-I',str(root),str(out/'test.c'),'-Wl,-Ttext=0x10000,-e,test_capture',
    '-lc','-lgcc','-o',str(binary)],check=True)
uc=Uc(UC_ARCH_ARM,UC_MODE_ARM)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
uc.reg_write(UC_ARM_REG_C1_C0_2,15<<20);uc.reg_write(UC_ARM_REG_FPEXC,1<<30)
elf_ranges=[];elf_write_ranges=[]
with binary.open('rb') as f:
    elf=ELFFile(f);symbols=elf.get_section_by_name('.symtab')
    functions={n:symbols.get_symbol_by_name(n)[0]['st_value'] for n in
               ['test_capture','test_capture_neon','test_equal','test_hooks','sceClibMemcpy','sceClibMemset']}
    pages=set()
    for segment in elf.iter_segments():
        if segment['p_type']!='PT_LOAD':continue
        lo,size=segment['p_vaddr'],segment['p_memsz']
        for page in range(lo&~4095,(lo+size+4095)&~4095,4096):
            if page not in pages:uc.mem_map(page,4096);pages.add(page)
        uc.mem_write(lo,segment.data());elf_ranges.append((lo,lo+size))
        if segment['p_flags'] & 2:elf_write_ranges.append((lo,lo+size))
A,B,R,STACK,END,CAP=0x200000,0x400000,0x600000,0x700000,0x900000,0x100000
for address,size in [(A,CAP),(B,CAP),(R,4096),(STACK,65536),(END,4096)]:uc.mem_map(address,size)
allowed_read=[];allowed_write=[];faults=[];calls=0

def access(emu,kind,address,size,value,user):
    allowed=allowed_read if kind==UC_MEM_READ else allowed_write
    image_ranges=elf_ranges if kind==UC_MEM_READ else elf_write_ranges
    if any(lo<=address and address+size<=hi for lo,hi in allowed+image_ranges+[(STACK,STACK+65536)]):return
    faults.append((kind,hex(address),size));emu.emu_stop()

uc.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,access)
def firmware(emu,address,size,user):
    dst,value,n=(emu.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2))
    if not n:return
    access(emu,UC_MEM_WRITE,dst,n,0,None)
    if address==(functions['sceClibMemcpy']&~1):
        access(emu,UC_MEM_READ,value,n,0,None);data=bytes(emu.mem_read(value,n))
    else:data=bytes([value&255])*n
    assert not faults,faults
    emu.mem_write(dst,data)
for name in ['sceClibMemcpy','sceClibMemset']:
    address=functions[name]&~1;uc.hook_add(UC_HOOK_CODE,firmware,begin=address,end=address)

def invoke(name,*args):
    global calls
    for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):uc.reg_write(reg,value)
    uc.reg_write(UC_ARM_REG_SP,STACK+65024);uc.reg_write(UC_ARM_REG_LR,END|1)
    uc.emu_start(functions[name],END,count=10000000)
    assert not faults and uc.reg_read(UC_ARM_REG_PC)==END,(faults,hex(uc.reg_read(UC_ARM_REG_PC)))
    calls+=1;return uc.reg_read(UC_ARM_REG_R0)

allowed_write=[(A,A+4)]
if invoke('test_hooks',A)!=1 or bytes(uc.mem_read(A,4))!=b'\x01\x00\x00\x00':
    raise SystemExit(f'Unicorn {unicorn.__version__} failed the Thumb IT/memory-hook self-check. '
        'This emulator cannot validate these helpers with strict access hooks. '
        'Use a separate environment with unicorn==2.1.3 and pyelftools; do not disable bounds hooks. '
        'See https://github.com/unicorn-engine/unicorn/issues/2309.')
capture_start=calls
rng=random.Random(20260908)
def capture(values,a,b):
    global allowed_read,allowed_write
    data=struct.pack('<'+'H'*len(values),*values)
    uc.mem_write(a,data);uc.mem_write(R,b'\xa5'*1032)
    allowed_read=[(a,a+len(data)),(R,R+1032)];allowed_write=[(b,b+len(data)),(R,R+1032)]
    vertices=max(values)+1 if values else 0;groups={v>>3 for v in values};bits=[0]*256
    for g in groups:bits[g>>5]|=1<<(g&31)
    expected=struct.pack('<258I',*bits,vertices,len(groups))
    for name in ['test_capture','test_capture_neon']:
        uc.mem_write(R,b'\xa5'*1032);uc.mem_write(b,b'\x5a'*len(data))
        assert invoke(name,b,a,len(values),R)==vertices
        assert bytes(uc.mem_read(b,len(data)))==data
        assert bytes(uc.mem_read(R,1032))==expected
        assert bytes(uc.mem_read(a,len(data)))==data
    return vertices,groups

counts=[0,1,7,8,9,31,32,33,255,256,257,1023,1024,4096]
for count in counts:
    for offset in range(16):
        capture([rng.randrange(65536) for _ in range(count)],A+offset,B+15-offset)
capture(list(range(65536)),A+3,B+1)
for count in [1,7,8,255,256,257,4096]:
    capture([rng.randrange(65536) for _ in range(count)],A+CAP-count*2,B+CAP-count*2)
# Exercise the vector reduction and scalar tail with a lone maximum in every
# lane, both sides of the 256-index cutoff and every bitmap word boundary.
for n in [255,256,257,263,264]:
    for pos in range(n):
        values=[0]*n;values[pos]=65535;capture(values,A+1,B+3)
for word in range(256):
    capture([word*256,word*256+255]*128,A+3,B+1)
capture_cases=calls-capture_start
for vertices in [511,512,513,1024,1025,65535,65536]:
    for stride in [1,3,8,12]:
        values=[0,vertices-1]+[rng.randrange(vertices) for _ in range(8)]
        n,groups=capture(values,A,B)
        selected=set(range(vertices)) if vertices<512 or len(groups)*16>=vertices else {
            v for g in groups for v in range(g*8,min(g*8+8,vertices))}
        selected_bytes={v*stride+b for v in selected for b in range(stride)}
        runs=[]
        for v in sorted(selected):
            a,b=v*stride,(v+1)*stride
            if runs and runs[-1][1]==a:runs[-1][1]=b
            else:runs.append([a,b])
        size=vertices*stride;left=A+CAP-size;right=B+CAP-size
        data=rng.randbytes(size);uc.mem_write(left,data)
        mutations=[None,0,size-1,values[2]*stride,min(size-1,257*stride),min(size-1,511*stride)]
        for changed in mutations:
            changed_data=bytearray(data)
            if changed is not None:changed_data[changed]^=0x80
            uc.mem_write(right,bytes(changed_data))
            allowed_read=[(left+a,left+b) for a,b in runs]+[(right+a,right+b) for a,b in runs]+[(R,R+1032)]
            allowed_write=[]
            expected=int(changed is None or changed not in selected_bytes)
            assert invoke('test_equal',left,right,R,stride)==expected,(vertices,stride,changed)
# Instruction counts describe generated work only; firmware bodies, memory
# latency, CPU cycles and FPS are not modeled by this harness.
instructions=0
copy_calls=copy_bytes=0
def count_instruction(emu,address,size,user):
    global instructions,copy_calls,copy_bytes
    instructions+=1
    if address==(functions['sceClibMemcpy']&~1):
        copy_calls+=1;copy_bytes+=emu.reg_read(UC_ARM_REG_R2)
hook=uc.hook_add(UC_HOOK_CODE,count_instruction)
for lo,hi in elf_ranges:uc.ctl_remove_cache(lo,hi)
rows=[]
for n in [128,256,512,4096]:
    for pattern in ['repeated-high','sequential','random']:
        values=[65535]*n if pattern=='repeated-high' else list(range(n)) if pattern=='sequential' else [rng.randrange(65536) for _ in range(n)]
        capture(values,A+1,B+3)
        row={'indices':n,'pattern':pattern}
        for name in ['test_capture','test_capture_neon']:
            instructions=copy_calls=copy_bytes=0
            assert invoke(name,B+3,A+1,n,R)==max(values)+1
            row[name]={'instructions':instructions,'firmware_copy_calls':copy_calls,'firmware_copy_bytes':copy_bytes}
        rows.append(row)
uc.hook_del(hook)
result=dict(index_capture_cases=capture_cases,total_calls=calls,strict_access_bounds=True,
            unicorn_version=unicorn.__version__,thumb_memory_hook_self_check=True,
            elf_writes_restricted_to_writable_segments=True,
            elf_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
            index_header_sha256=hashlib.sha256((root/'runtime/xv_index_copy.h').read_bytes()).hexdigest(),
            instruction_counts_not_cycles=rows,
            modeled_firmware=['sceClibMemcpy','sceClibMemset'],performance_measured=False)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS:',json.dumps(result))
