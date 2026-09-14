#!/usr/bin/env python3
"""Check Vita-compiled empty-object scans against private original slices.

Use original.c from test_object_scan.py. Requires VitaSDK, Unicorn and
pyelftools. Reports instructions, not CPU cycles or gameplay performance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
    UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_FPSCR, UC_ARM_REG_R0, UC_ARM_REG_R1,
    UC_ARM_REG_R2, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

ROOT = Path(__file__).resolve().parents[1]
RAM, PT, STACK, CTX, END = 0x20000000, 0x21000000, 0x22000000, 0x23000000, 0x24000000
SIZE = 4 << 20


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--reference', type=Path, required=True)
    ap.add_argument('--output-dir', type=Path, required=True)
    ap.add_argument('--cases', type=int, default=4096)
    args = ap.parse_args()
    if not 1 <= args.cases <= 20000:
        ap.error('--cases must be from 1 through 20000')
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    harness = out / 'harness.c'
    harness.write_text('''#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram, *g_img_base; uint32_t *g_xpt;
unsigned reference_end;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,preempt),
    offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,f_cf_override),
    offsetof(xctx,f_of_override),offsetof(xctx,f_cf),offsetof(xctx,f_of)};
void test_boot(void) {}
void xk_os_log(const char *fmt,...) {(void)fmt;}
char *getenv(const char *name) {(void)name;return 0;}
int atoi(const char *value) {(void)value;return 0;}
void __wrap_xv_preempt(xctx *c) {(void)c;__builtin_trap();}
void *memcpy(void *d,const void *s,size_t n) {(void)s;(void)n;return d;}
''')
    elf_path = out / 'arm-test.elf'
    command = ['arm-vita-eabi-gcc', '-O2', '-fno-strict-aliasing', '-mthumb',
        '-mcpu=cortex-a9', '-mfpu=neon', '-std=gnu11',
        '-I' + str(ROOT / 'recomp'), '-DXV_NATIVE_OBJECT_SCAN',
        '-ffunction-sections', '-fdata-sections', str(args.reference), str(harness),
        str(ROOT / 'recomp/kernel/xk_object_scan.c'), str(ROOT / 'recomp/xv_x86rt.c'),
        '-nostdlib', '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,'
        '--undefined=original_scan_0,--undefined=original_scan_1,--undefined=original_scan_2,'
        '--undefined=xv_object_scan_empty,--undefined=xv_object_scan_override,--undefined=layout',
        '-lgcc', '-o', str(elf_path)]
    subprocess.run(command, check=True)
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    uc.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
    uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
    with elf_path.open('rb') as file:
        elf = ELFFile(file)
        symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
        segments = [s for s in elf.iter_segments() if s['p_type']=='PT_LOAD']
        for page in sorted({p for s in segments for p in range(
                s['p_vaddr'] & ~4095, (s['p_vaddr']+s['p_memsz']+4095)&~4095, 4096)}):
            uc.mem_map(page,4096)
        for segment in segments:
            uc.mem_write(segment['p_vaddr'],segment.data())
    fields = ('size','r','preempt','kind','bits','cf_override','of_override','cf','of')
    layout = dict(zip(fields,struct.unpack('<9I',uc.mem_read(symbols['layout'],36))))
    for address, size in ((RAM,SIZE),(PT,4<<20),(STACK,65536),(CTX,4096),(END,4096)):
        uc.mem_map(address,size)
    for name,value in (('g_xram',RAM),('g_img_base',RAM),('g_xpt',PT)):
        uc.mem_write(symbols[name],struct.pack('<I',value))
    pages = [(i^1)*4096 for i in range(SIZE//4096)]
    uc.mem_write(PT,struct.pack('<'+'I'*len(pages),*pages))
    instructions = [0]
    memcpy_address = symbols.get('memcpy', 0) & ~1
    def step(uc,address,size,user):
        instructions[0] += 1
        if memcpy_address and address == memcpy_address:
            dst,src,n=(uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2))
            assert n <= SIZE
            uc.mem_write(dst,bytes(uc.mem_read(src,n)))
            uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
    uc.hook_add(UC_HOOK_CODE,step)
    def invoke(name,r0,r1=0,fpscr=0):
        uc.reg_write(UC_ARM_REG_R0,r0);uc.reg_write(UC_ARM_REG_R1,r1)
        uc.reg_write(UC_ARM_REG_SP,STACK+65024)
        uc.reg_write(UC_ARM_REG_LR,END|1);uc.reg_write(UC_ARM_REG_FPSCR,fpscr)
        instructions[0]=0
        uc.emu_start(symbols[name]|1,END,count=1000000)
        assert uc.reg_read(UC_ARM_REG_PC)==END, name+' did not return'
        return uc.reg_read(UC_ARM_REG_R0),instructions[0],uc.reg_read(UC_ARM_REG_FPSCR)
    invoke('xv_object_scan_override',1)
    uc.mem_write(CTX,bytes(layout['size']))
    invoke('xv_object_scan_empty',CTX)
    rows=[]
    for k in range(args.cases):
        rng=random.Random(92371+k)
        arena=bytearray(b'\xa5'*SIZE)
        context=bytearray(rng.getrandbits(8) for _ in range(layout['size']))
        def word(offset,value):
            struct.pack_into('<I',context,offset,value&0xffffffff)
        address=0x21000+k%4
        variant=(k//3)%12
        if variant==1:address+=4088
        if variant==2:address+=4095
        if variant==3:address=0xffffffe0
        index=rng.randrange(300);limit=index+rng.randrange(350)+1;budget=rng.randrange(200)-2
        if variant==4:index=0xffff
        if variant==5:limit=-1
        if variant==6:limit=index+1
        if variant==7:budget=1
        table=0x18000+(4049 if variant==8 else 0)
        run=0 if variant==9 else 350 if variant==10 else rng.randrange(200)
        def put16(guest,value):
            guest &= 0xffffffff
            page=pages[guest>>12] if guest>>12<len(pages) else 0
            struct.pack_into('<H',arena,page+(guest&4095),value&0xffff)
        struct.pack_into('<I',arena,0x2fc6ac,table)
        put16(table+0x2e,limit)
        for n in range(run):put16(address+n*12,0)
        put16(address+run*12,0x4567)
        for reg,value in ((5,table),(6,address),(7,index)):word(layout['r']+reg*4,value)
        word(layout['preempt'],budget);word(layout['kind'],rng.randrange(6))
        word(layout['bits'],(8,16,32)[rng.randrange(3)])
        for field in ('cf_override','of_override','cf','of'):word(layout[field],rng.randrange(2))
        fpscr=(0,0x9f,0xc00010,0x300009f)[k%4]
        for pass_index in range(3):
            uc.mem_write(RAM,bytes(arena));uc.mem_write(CTX,bytes(context))
            skipped,candidate_count,candidate_fp=invoke('xv_object_scan_empty',CTX,pass_index,fpscr)
            actual=bytes(uc.mem_read(CTX,layout['size']))
            assert bytes(uc.mem_read(RAM,SIZE))==arena, (k,pass_index,'candidate memory')
            assert skipped<=128 and (not skipped or 0<skipped<budget), (k,skipped,budget)
            assert candidate_fp==fpscr
            uc.mem_write(CTX,bytes(context))
            uc.mem_write(symbols['reference_end'],struct.pack('<I',(index+skipped)&0xffffffff))
            _,baseline_count,baseline_fp=invoke('original_scan_'+str(pass_index),CTX,0,fpscr)
            expected=bytes(uc.mem_read(CTX,layout['size']))
            if actual!=expected:
                (out/'mismatch.json').write_text(json.dumps(dict(case=k,pass_index=pass_index,
                    skipped=skipped,offsets=[i for i,(a,e) in enumerate(zip(actual,expected)) if a!=e])))
                raise AssertionError((k,pass_index,'context'))
            assert bytes(uc.mem_read(RAM,SIZE))==arena and baseline_fp==fpscr
            rows.append(dict(case=k,pass_index=pass_index,skipped=skipped,
                             baseline=baseline_count,candidate=candidate_count))
    accepted=[x for x in rows if x['skipped']]
    report=dict(cases=len(rows),accepted=len(accepted),entries=sum(x['skipped'] for x in accepted),
        accepted_instructions={key:sum(x[key] for x in accepted) for key in ('baseline','candidate')},
        units='ARM instructions, not cycles or FPS',command=command,
        elf_sha256=hashlib.sha256(elf_path.read_bytes()).hexdigest(),
        reference_sha256=hashlib.sha256(args.reference.read_bytes()).hexdigest())
    (out/'cases.json').write_text(json.dumps(rows,indent=2)+'\n')
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
