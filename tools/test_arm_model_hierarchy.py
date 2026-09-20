#!/usr/bin/env python3
"""Check Vita-compiled child batches against private original and current lifts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

import test_arm_model_palette as base
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPSCR

ROOT=Path(__file__).resolve().parents[1]
base.SIZE=4<<20
RAM,PT,STACK,CTX,END,ENV=base.RAM,base.PT,base.STACK,base.CTX,base.END,base.ENV


def build(directory,reference,cc,assist=False):
    harness=directory/'arm-fixture.c'
    harness.write_text('''#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void test_boot(void) {}
void xk_os_log(const char *fmt,...) { (void)fmt; }
char *getenv(const char *name) { (void)name;return 0; }
int atoi(const char *s) { (void)s;return 0; }
void __wrap_xv_preempt(xctx *c) { c->preempt=100; }
void *memcpy(void *d,const void *s,size_t n) { (void)s;(void)n;return d; }
void *memmove(void *d,const void *s,size_t n) { (void)s;(void)n;return d; }
void *memset(void *d,int s,size_t n) { (void)s;(void)n;return d; }
/* Unreachable watch-diagnostic imports: the instruction harness rejects calls. */
int snprintf(char *s,size_t n,const char *fmt,...) { (void)s;(void)n;(void)fmt;return 0; }
unsigned long strtoul(const char *s,char **end,int radix) { (void)s;(void)end;(void)radix;return 0; }
int sceClibPrintf(const char *fmt,...) { (void)fmt;return 0; }
''')
    if assist:
        with harness.open('a') as output:
            output.write(r'''
/* Simulated helper FP context, not a concurrency or scheduling model. */
static void (*captured_run)(void *);
static void *captured_argument;
unsigned assist_runs;
int xv_object_hierarchy_offer(xctx *c,int guard,void (*run)(void *),void *argument)
{ (void)c;(void)guard;captured_run=run;captured_argument=argument;return 1; }
void xv_object_hierarchy_join(int token)
{
    (void)token;unsigned owner,helper=0x0bc0009fu,after;
    __asm__ volatile("vmrs %0, fpscr":"=r"(owner)::"memory");
    __asm__ volatile("vmsr fpscr, %0"::"r"(helper):"memory");
    __asm__ volatile("vmrs %0, fpscr":"=r"(helper)::"memory");
    captured_run(captured_argument);
    __asm__ volatile("vmrs %0, fpscr":"=r"(after)::"memory");
    if(after!=helper)__builtin_trap();
    __asm__ volatile("vmsr fpscr, %0"::"r"(owner):"memory");
    assist_runs++;
}
void xv_object_hierarchy_assist_report(unsigned frames) { (void)frames; }
''')
    common=[cc,'-std=gnu11','-fno-strict-aliasing','-ffp-contract=off','-mthumb',
        '-mcpu=cortex-a9','-mfpu=neon','-I'+str(ROOT/'recomp'),'-DXV_NATIVE_MODEL_HIERARCHY','-DXV_NATIVE_MATRIX_NEON',
        '-ffunction-sections','-fdata-sections',*(['-DXV_HIERARCHY_ASSIST=1'] if assist else [])]
    sources=[reference,harness,ROOT/'recomp/kernel/xk_hierarchy.c',ROOT/'recomp/kernel/xk_math.c',ROOT/'recomp/xv_x86rt.c']
    objects=[];commands=[]
    for i,path in enumerate(sources):
        obj=directory/f'unit-{i}.o';objects.append(str(obj))
        opt=['-O3'] if path.name in ('xk_hierarchy.c','xk_math.c') else ['-O2']
        if path.name=='xk_math.c':opt+=['-funroll-loops']
        command=common+opt+['-c',str(path),'-o',str(obj)]
        subprocess.run(command,check=True);commands.append(command)
    elf=directory/'arm-test.elf'
    command=common+objects+['-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,'
        '--undefined=original_hierarchy,--undefined=current_hierarchy,--undefined=candidate_hierarchy,'
        '--undefined=xv_math_model_hierarchy,--undefined=layout','-lgcc','-o',str(elf)]
    subprocess.run(command,check=True);commands.append(command)
    return elf,commands


class Machine(base.Machine):
    def __init__(self,path,enabled='on'):
        super().__init__(path,enabled)
        self.unmodeled={self.symbols[n]&~1:n for n in ('snprintf','strtoul','sceClibPrintf') if n in self.symbols}

    def step(self,uc,address,size,user):
        if address in self.unmodeled:raise RuntimeError('Unexpected diagnostic import '+self.unmodeled[address])
        if self.imports.get(address)=='getenv':
            self.instructions+=1
            key=bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0),64)).split(b'\0')[0]
            value=ENV if key==b'XV_NATIVE_MATH' else 0
            if key==b'XV_NATIVE_MODEL_HIERARCHY':value={'on':ENV,'off':ENV+2,'unset':0}[self.enabled]
            uc.reg_write(UC_ARM_REG_R0,value);uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))
        else:super().step(uc,address,size,user)

    def run(self,function,fixture):
        memory,context,pages,fpscr=fixture;u=self.uc
        u.mem_write(RAM,memory);u.mem_write(CTX,context)
        u.mem_write(PT,struct.pack('<'+'I'*len(pages),*pages));u.mem_write(STACK,bytes(65536))
        u.reg_write(UC_ARM_REG_R0,CTX);u.reg_write(UC_ARM_REG_SP,STACK+65024)
        u.reg_write(UC_ARM_REG_LR,END|1);u.reg_write(UC_ARM_REG_FPSCR,fpscr)
        actual_control=u.reg_read(UC_ARM_REG_FPSCR)
        self.instructions=self.copies=self.copy_bytes=self.yields=0
        names=('batches','prepared')+(('assist_runs',) if 'assist_runs' in self.symbols else ())
        before={n:struct.unpack('<I',u.mem_read(self.symbols[n],4))[0] for n in names}
        u.emu_start(self.symbols[function]|1,END,count=2000000)
        assert u.reg_read(UC_ARM_REG_PC)==END,function+' did not return'
        delta={n:(struct.unpack('<I',u.mem_read(self.symbols[n],4))[0]-before[n])&0xffffffff for n in names}
        return dict(result=u.reg_read(UC_ARM_REG_R0),fpscr=u.reg_read(UC_ARM_REG_FPSCR),initial_fpscr=actual_control,
            context=bytes(u.mem_read(CTX,self.layout['size'])),memory=bytes(u.mem_read(RAM,base.SIZE)),
            instructions=self.instructions,copies=self.copies,copy_bytes=self.copy_bytes,yields=self.yields,**delta)


def fixture(layout,count,shape,first,fpscr,variant=0):
    memory=bytearray(b'\xa5'*base.SIZE);context=bytearray(layout['size'])
    pages=[(i^0x40)*4096 for i in range(base.SIZE//4096)]
    model,pose,nodes,matrices,sp,obj=0x12000,0x22fc0,0x32fc0,0x52fc0,0x62800,0x72000
    def write(a,data):
        for i,v in enumerate(data):memory[pages[(a+i)>>12]+((a+i)&4095)]=v
    def word(a,v):write(a,struct.pack('<I',v))
    def field(n,v,fmt='I'):struct.pack_into('<'+fmt,context,layout[n],v)
    def reg(n,v):struct.pack_into('<I',context,layout['r']+4*n,v)
    for i in range(8):
        reg(i,0x12345000+i);struct.pack_into('<d',context,layout['st']+8*i,i+.375)
        for j in range(4):struct.pack_into('<f',context,layout['xmm']+4*(4*i+j),i*4+j+.125)
    reg(0,first);reg(4,sp);reg(5,obj);field('fsp',count%8);field('fsw',0xabcd,'H');field('fcw',0x37f,'H')
    field('preempt',1 if variant==1 else count+10);field('f_kind',3);field('f_bits',32)
    word(model+0xb8,count);word(model+0xbc,nodes);word(sp+0x24,matrices);word(sp+0x28,pose);word(sp+0x2c,model);word(sp+0x20,first)
    write(sp+0x17,b'\x01');word(0x1f0a68,0);word(0x1f0a78,0x3f800000);word(0x1f0b04,0x40000000)
    order=list(range(count))
    if shape==2:order=[0]+list(reversed(order[1:]))
    queued=min(first+1 if shape==0 else 2*first+1,count);word(sp+0x10,queued)
    for i,n in enumerate(order[:queued]):write(sp+0x178+2*i,struct.pack('<h',n))
    for i,n in enumerate(order):
        a,b=(i+1,-1) if shape==0 else (2*i+1,2*i+2)
        links=(order[a] if a<count else -1,order[b] if 0<=b<count else -1,
               order[i-1 if shape==0 else (i-1)//2] if i else -1)
        write(nodes+n*156+0x20,struct.pack('<3h',*links))
        for j in range(8):write(pose+n*32+4*j,struct.pack('<f',1.0 if j==7 else ((n*7+j*3)%17-8)*.037))
        for j in range(13):write(matrices+n*52+4*j,struct.pack('<f',1.0 if j in (0,1,5,9) else 0.0))
    if variant==2:
        # Zero quaternion plus signed-zero translation: valid arithmetic.
        for j in range(7):word(pose+order[first]*32+j*4,0x80000000 if j&1 else 0)
    elif variant==3:word(pose+order[first]*32,0x7fc01234)
    elif variant==4:word(pose+order[first]*32,1)
    elif variant==5:
        word(pose+order[first]*32+28,0x4e800000);word(matrices,0x4e800000)
    elif variant==6:pages[(pose>>12)+1]=pages[(pose>>12)+4]
    return bytes(memory),bytes(context),pages,fpscr


def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--reference',type=Path,required=True)
    ap.add_argument('--output-dir',type=Path,required=True);ap.add_argument('--enabled',choices=('on','off','unset'),default='on')
    ap.add_argument('--assist',action='store_true',help='Exercise callback in a distinct simulated helper FP context')
    ap.add_argument('--suite',choices=('full','extended'),default='full')
    ap.add_argument('--cc',default=os.environ.get('ARM_CC','arm-vita-eabi-gcc'))
    a=ap.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True);elf,commands=build(a.output_dir,a.reference,a.cc,a.assist)
    m=Machine(elf,a.enabled);rows=[]
    def check(count,shape,first,rounding,control,variant):
        sample=fixture(m.layout,count,shape,first,(rounding<<22)|control,variant)
        row=dict(rounding=rounding,control=control,count=count,shape=shape,first=first,variant=variant)
        admission=m.run('xv_math_model_hierarchy',sample)
        if not admission['result']:
            assert admission['context']==sample[1] and admission['memory']==sample[0],row
            assert admission['fpscr']==admission['initial_fpscr'] and not admission['yields'],row
        if a.enabled!='on':assert not admission['result'],row
        names=('current','candidate','original') if variant in (0,1,2) else ('current','candidate')
        results={n:m.run(n+'_hierarchy',sample) for n in names};ref=results['current']
        for name in names:
            for key in ('context','memory','fpscr','yields'):
                if results[name][key]!=ref[key]:
                    target=a.output_dir/f'mismatch-{len(rows)}-{name}-{key}'
                    target.with_suffix('.json').write_text(json.dumps(row,indent=2))
                    if key in ('context','memory'):
                        target.with_suffix('.expected').write_bytes(ref[key]);target.with_suffix('.actual').write_bytes(results[name][key])
                    else:target.with_suffix('.values').write_text(repr((ref[key],results[name][key])))
                    raise AssertionError(str(target))
        row['admitted']=admission['result'];row['runs']={n:{k:v for k,v in r.items() if k not in ('context','memory')} for n,r in results.items()};rows.append(row)
    if a.suite=='full':
        for rounding in range(4):
            for control in (0,0x9f,0x01000000,0x02000000,0x0300009f):
                for count in (3,4,8,16,32,64):
                    for shape in range(3):
                        for variant in (0,1,2,3,4,5):check(count,shape,1,rounding,control,variant)
            print('PASS ARM rounding',rounding,'comparisons',len(rows),flush=True)
    for rounding in range(4):
        for count in (8,32):
            for shape in range(3):
                for first in (0,2,count-2):
                    for control in (0,0x0300009f):check(count,shape,first,rounding,control,0)
        # Noncontiguous page mappings take the complete original fallback.
        check(8,0,1,rounding,0,6)
    print('PASS ARM root, consumed prefixes, shuffled indices and remapped input pages:',len(rows),flush=True)
    if a.assist and a.enabled=='on':
        assert sum(r['runs']['candidate']['assist_runs'] for r in rows)>0
    report=dict(assist=a.assist,fixtures=len(rows),suite=a.suite,enabled=a.enabled,commands=commands,elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
        limitation='Cortex-A9 instruction emulator, modeled memory-copy imports; not cycles or hardware FPS',rows=rows)
    (a.output_dir/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    for r in rows:
        if r['rounding']==r['control']==r['shape']==r['variant']==0:
            print('nodes',r['count'],'current',r['runs']['current']['instructions'],'candidate',r['runs']['candidate']['instructions'],'admitted',r['admitted'])


if __name__=='__main__':main()
