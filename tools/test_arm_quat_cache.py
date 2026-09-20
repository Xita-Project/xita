#!/usr/bin/env python3
"""Validate Vita-compiled quaternion reuse against current and original math.

Uses the private reference emitted by test_quat_cache.py. Requires VitaSDK,
Unicorn and pyelftools. Imported memory routines are modeled: instruction
counts do not represent cycles or hardware frame times.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess

import test_arm_model_palette as arm
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR)

ROOT = Path(__file__).resolve().parents[1]
arm.SIZE = 2 << 20


def build(directory, reference, cc):
    harness = directory / 'arm-fixture.c'
    harness.write_text('''#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
const unsigned layout[] = {sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void test_boot(void) {}
void xk_os_log(const char *format, ...) { (void)format; }
char *getenv(const char *name) { (void)name; return 0; }
int atoi(const char *value) { (void)value; return 0; }
void *memcpy(void *d,const void *s,size_t n) { (void)s;(void)n;return d; }
void *memmove(void *d,const void *s,size_t n) { (void)s;(void)n;return d; }
void *memset(void *d,int v,size_t n) { (void)v;(void)n;return d; }
int memcmp(const void *a,const void *b,size_t n) { (void)a;(void)b;(void)n;return 0; }
void original_quaternion(xctx *);
int current_quaternion(xctx *), xv_math_quaternion_matrix(xctx *);
void current_wrapper(xctx *c) { if(!current_quaternion(c))original_quaternion(c); }
void candidate_wrapper(xctx *c) { if(!xv_math_quaternion_matrix(c))original_quaternion(c); }
''')
    flags = ['-O2', '-fno-strict-aliasing', '-ffp-contract=off', '-mthumb',
             '-mcpu=cortex-a9', '-mfpu=neon', '-std=gnu11', '-I'+str(ROOT/'recomp'),
             '-ffunction-sections', '-fdata-sections']
    baseline = directory / 'baseline.o'
    command1 = [cc, *flags, '-Dxv_math_quaternion_matrix=current_quaternion',
                '-Dxv_math_matrix_multiply=current_matrix',
                '-Dxv_math_point_transform=current_point',
                '-Dxv_point_math_override=current_point_override',
                '-Dxv_native_math_report=current_report', '-c',
                str(ROOT/'recomp/kernel/xk_math.c'), '-o', str(baseline)]
    subprocess.run(command1, check=True)
    elf = directory/'arm-test.elf'
    command2 = [cc, *flags, '-DXV_QUAT_CACHE', str(reference), str(harness),
                str(baseline), str(ROOT/'recomp/kernel/xk_math.c'),
                str(ROOT/'recomp/kernel/xk_quat_cache.c'), str(ROOT/'recomp/xv_x86rt.c'),
                '-nostdlib', '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,'
                '--undefined=original_quaternion,--undefined=current_wrapper,'
                '--undefined=candidate_wrapper,--undefined=layout', '-lgcc', '-o', str(elf)]
    subprocess.run(command2, check=True)
    return elf, [command1, command2]


class Machine(arm.Machine):
    def __init__(self, path):
        super().__init__(path)
        self.imports[self.symbols['memcmp'] & ~1] = 'memcmp'

    def step(self, uc, address, size, user):
        if self.imports.get(address) != 'memcmp':
            return super().step(uc, address, size, user)
        self.instructions += 1
        a,b,n = (uc.reg_read(r) for r in (UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2))
        left,right = bytes(uc.mem_read(a,n)),bytes(uc.mem_read(b,n))
        value = next((x-y for x,y in zip(left,right) if x!=y),0)
        self.compares += 1
        self.compare_bytes += n
        uc.reg_write(UC_ARM_REG_R0,value & 0xffffffff)
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))

    def run(self, function, sample):
        memory, context, pages, fpscr = sample
        uc = self.uc
        uc.mem_write(arm.RAM,memory);uc.mem_write(arm.CTX,context)
        uc.mem_write(arm.PT,struct.pack('<'+'I'*len(pages),*pages))
        uc.mem_write(arm.STACK,bytes(65536))
        uc.reg_write(UC_ARM_REG_R0,arm.CTX)
        uc.reg_write(UC_ARM_REG_SP,arm.STACK+65024)
        uc.reg_write(UC_ARM_REG_LR,arm.END|1)
        uc.reg_write(UC_ARM_REG_FPSCR,fpscr)
        self.instructions=self.copies=self.copy_bytes=self.yields=0
        self.compares=self.compare_bytes=0
        def counter(name): return struct.unpack('<I',uc.mem_read(self.symbols[name],4))[0]
        before={name:counter(name) for name in ('hits','misses','unsupported')}
        uc.emu_start(self.symbols[function]|1,arm.END,count=200000)
        assert uc.reg_read(UC_ARM_REG_PC)==arm.END,function+' did not return'
        return dict(memory=bytes(uc.mem_read(arm.RAM,arm.SIZE)),
                    context=bytes(uc.mem_read(arm.CTX,self.layout['size'])),
                    fpscr=uc.reg_read(UC_ARM_REG_FPSCR),instructions=self.instructions,
                    copies=self.copies,copy_bytes=self.copy_bytes,
                    compares=self.compares,compare_bytes=self.compare_bytes,
                    **{name:(counter(name)-before[name])&0xffffffff for name in before})


def fixture(layout, case, warm, rounding, mode):
    rng=random.Random(case)
    memory=bytearray(b'\xa5'*arm.SIZE)
    context=bytearray(layout['size'])
    pages=[(i^0x40)*4096 for i in range(arm.SIZE//4096)]
    a,out,sp=0x11080,0x21080,0x31800
    variant=case%10
    if variant==1: out=a  # Native guard rejects input/output alias.
    elif variant==2: a=0x11ffc;pages[0x12]=pages[0x15]
    elif variant==3: pages[out>>12]=pages[a>>12]
    elif variant==4: out+=1
    elif variant==5: sp=0x31010
    def write(address,data):
        for i,value in enumerate(data):
            memory[pages[(address+i)>>12]+((address+i)&4095)]=value
    def word(address,value):write(address,struct.pack('<I',value))
    def field(name,value,fmt='I'):struct.pack_into('<'+fmt,context,layout[name],value)
    def reg(index,value):struct.pack_into('<I',context,layout['r']+index*4,value)
    for i in range(8):
        reg(i,0x12345600+i)
        struct.pack_into('<d',context,layout['st']+i*8,i+warm+.125)
        for j in range(4):struct.pack_into('<f',context,layout['xmm']+(i*4+j)*4,i*4+j+.75)
    reg(1,a);reg(2,out);reg(4,sp)
    field('fsp',(case+warm*3)&7);field('fsw',(0xabcd^(warm*0x5317))&0xffff,'H')
    field('fcw',0x37f,'H');field('f_kind',3);field('f_bits',32)
    values=[0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,
            0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f800001,0x3f800000]
    for j in range(4):
        value=values[(case//10+j)%len(values)] if case%3==1 else struct.unpack('<I',struct.pack('<f',rng.uniform(-2,2)))[0]
        word(a+j*4,value)
    for address,value in ((0x1f0a68,1.0 if case%17==0 else 0.0),
                           (0x1f0a78,.5 if case%19==0 else 1.0),
                           (0x1f0b04,1.0 if case%23==0 else 2.0)):
        write(address,struct.pack('<f',value))
    fpscr=(rounding<<22)|mode|(0x9f if warm==1 else 0)
    return (bytes(memory),bytes(context),pages,fpscr),(out,sp)


def normalized(result,layout,pages,addresses):
    context=bytearray(result['context']);memory=bytearray(result['memory'])
    for i in range(8):
        offset=layout['st']+i*8
        if math.isnan(struct.unpack_from('<d',context,offset)[0]):context[offset:offset+8]=bytes(8)
    out,sp=addresses
    for address,length in ((out,52),(sp-24,24)):
        for i in range(0,length,4):
            positions=[pages[(address+i+j)>>12]+((address+i+j)&4095) for j in range(4)]
            if math.isnan(struct.unpack('<f',bytes(memory[p] for p in positions))[0]):
                for p in positions:memory[p]=0
    return bytes(context),bytes(memory)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--cc',default='arm-vita-eabi-gcc')
    args=parser.parse_args();args.output_dir.mkdir(parents=True,exist_ok=True)
    elf,commands=build(args.output_dir,args.reference,args.cc)
    machine=Machine(elf);rows=[]
    for rounding in range(4):
        for mode in (0,1<<24,1<<25,(1<<24)|(1<<25)):
            for case in range(40):
                for warm in range(3):
                    sample,addresses=fixture(machine.layout,case,warm,rounding,mode)
                    results={name:machine.run(function,sample) for name,function in
                             (('original','original_quaternion'),('current','current_wrapper'),('candidate','candidate_wrapper'))}
                    expected=normalized(results['original'],machine.layout,sample[2],addresses)
                    for field in ('context','memory'):
                        assert results['candidate'][field]==results['current'][field],('exact native/cache',rounding,mode,case,warm,field)
                    for name in ('current','candidate'):
                        actual=normalized(results[name],machine.layout,sample[2],addresses)
                        if actual!=expected:
                            stem=args.output_dir/f'mismatch-{rounding}-{mode}-{case}-{warm}-{name}'
                            for index,label in enumerate(('context','memory')):
                                stem.with_suffix('.'+label+'.expected').write_bytes(expected[index])
                                stem.with_suffix('.'+label+'.actual').write_bytes(actual[index])
                            raise AssertionError(str(stem))
                    # FPSCR condition flags are call-clobbered; arithmetic sticky
                    # status and all control bits must match the current native path.
                    assert (results['candidate']['fpscr']&0x0fffffff)==(results['current']['fpscr']&0x0fffffff),(rounding,mode,case,warm,results['current']['fpscr'],results['candidate']['fpscr'])
                    rows.append(dict(rounding=rounding,mode=mode,case=case,warm=warm,
                        **{name:{k:v for k,v in result.items() if k not in ('memory','context')} for name,result in results.items()}))
        print(f'PASS ARM rounding {rounding}: {len(rows)} comparisons',flush=True)
    report=dict(fixtures=len(rows),commands=commands,elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                limitation='Instruction counts exclude modeled memory routine bodies; not CPU cycles or Vita FPS',rows=rows)
    (args.output_dir/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    for row in rows:
        if row['rounding']==row['mode']==row['case']==0:
            print('ARM warm',row['warm'],'current',row['current'],'candidate',row['candidate'])


if __name__=='__main__':main()
