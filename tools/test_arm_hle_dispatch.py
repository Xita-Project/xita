#!/usr/bin/env python3
"""Run the real indirect dispatcher and callback fixture as Cortex-A9 code.

Requires VitaSDK, Unicorn and pyelftools. No game assets are used. Instruction
counts exclude modeled environment calls and are not cycles or Vita FPS.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_C1_C0_2,
    UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR, UC_ARM_REG_R0, UC_ARM_REG_R1,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

ROOT = Path(__file__).resolve().parents[1]
RAM, PT, STACK, CTX, END, ENV = 0x20000000, 0x21000000, 0x22000000, 0x23000000, 0x24000000, 0x25000000


def build(out, cc, traced):
    source = out / 'fixture.c'
    source.write_text('''#define XV_RT_LOG(...) ((void)0)
#define main unused_host_main
#include ''' + json.dumps(str(ROOT / 'recomp/host/hle_dispatch_cache_test.c')) + '''
#undef main
unsigned cache_mode, call_target;
const unsigned context_size = sizeof(xctx);
void test_boot(void) {}
void sample_entry(xctx *c) {
    xv_hle_dispatch_override((int)cache_mode);
    xv_cur_fn=0x1234;
    xv_call(c,call_target);
}
__attribute__((noinline,noipa))
char *getenv(const char *name) { (void)name; return NULL; }
__attribute__((noinline,noipa))
int atoi(const char *value) { (void)value; return 0; }
int snprintf(char *s, size_t n, const char *format, ...) {
    (void)s; (void)n; (void)format; for(;;){}
}
void abort(void) { for(;;){} }
void __assert_func(const char *file,int line,const char *fn,const char *check) {
    (void)file;(void)line;(void)fn;(void)check;for(;;){}
}
''')
    elf = out / 'test.elf'
    command = [cc, '-O2', '-fno-strict-aliasing', '-mthumb', '-mcpu=cortex-a9',
        '-mfpu=neon', '-std=gnu11', '-ffunction-sections', '-fdata-sections',
        *([] if traced else ['-DTEST_UNTRACED']), str(source), '-nostdlib',
        '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--undefined=sample_entry,--undefined=context_size',
        '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True)
    return elf, command


class Machine:
    def __init__(self, path):
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        uc.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
        uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        with path.open('rb') as f:
            elf = ELFFile(f)
            self.syms = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
            segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
            for p in sorted({p for s in segments for p in range(s['p_vaddr'] & ~4095,
                             (s['p_vaddr'] + s['p_memsz'] + 4095) & ~4095, 4096)}):
                uc.mem_map(p, 4096)
            for s in segments:
                uc.mem_write(s['p_vaddr'], s.data())
        for address, size in ((RAM,4096),(PT,4<<20),(STACK,65536),(CTX,4096),(END,4096),(ENV,4096)):
            uc.mem_map(address,size)
        uc.mem_write(ENV,b'1\0')
        self.word('g_xram',RAM);self.word('g_xpt',PT)
        self.size = self.read_word('context_size')
        self.imports = {self.syms[n] & ~1: n for n in ('getenv','atoi','abort','__assert_func','snprintf') if n in self.syms}
        uc.hook_add(UC_HOOK_CODE,self.step)

    def word(self,name,value):
        self.uc.mem_write(self.syms[name],struct.pack('<I',value))

    def read_word(self,name):
        return struct.unpack('<I',self.uc.mem_read(self.syms[name],4))[0]

    def step(self,uc,address,size,user):
        self.instructions += 1
        name = self.imports.get(address)
        if name in ('abort','__assert_func','snprintf'):
            raise AssertionError('ARM callback fixture assertion: '+name)
        if name == 'getenv':
            key = bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0),64)).split(b'\0')[0]
            uc.reg_write(UC_ARM_REG_R0,ENV if key == b'XV_LENIENT' else 0)
        elif name == 'atoi':
            uc.reg_write(UC_ARM_REG_R0,1)
        else:
            return
        uc.reg_write(UC_ARM_REG_PC,uc.reg_read(UC_ARM_REG_LR))

    def run(self,context,memory,target,mode,fpscr):
        uc=self.uc
        uc.mem_write(CTX,context);uc.mem_write(RAM,memory);uc.mem_write(STACK,bytes(65536))
        self.word('cache_mode',mode);self.word('call_target',target)
        uc.reg_write(UC_ARM_REG_R0,CTX);uc.reg_write(UC_ARM_REG_SP,STACK+65024)
        uc.reg_write(UC_ARM_REG_LR,END|1);uc.reg_write(UC_ARM_REG_FPSCR,fpscr)
        self.instructions=0
        names=('callbacks','hles','magic_calls','hle_dispatch_lookups','hle_dispatch_hits','hle_dispatch_stores')
        before={n:self.read_word(n) for n in names}
        uc.emu_start(self.syms['sample_entry']|1,END,count=100000)
        assert uc.reg_read(UC_ARM_REG_PC)==END
        counts={n:(self.read_word(n)-before[n])&0xffffffff for n in names}
        return dict(context=bytes(uc.mem_read(CTX,self.size)),memory=bytes(uc.mem_read(RAM,4096)),
                    fpscr=uc.reg_read(UC_ARM_REG_FPSCR),marker=self.read_word('xv_cur_fn'),
                    instructions=self.instructions,**counts)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--cc',default='arm-vita-eabi-gcc')
    args=parser.parse_args();args.output_dir.mkdir(parents=True,exist_ok=True)
    report=[]
    targets=(0x11000,0x22000,0x33000,0x33400,0x33001,0x33004,0xfe000001,0xfe000002,0)
    for traced in (False,True):
        out=args.output_dir/('traced' if traced else 'untraced');out.mkdir(exist_ok=True)
        elf,command=build(out,args.cc,traced);machine=Machine(elf);rows=[]
        for i in range(4096):
            context=bytearray([i&255])*machine.size
            struct.pack_into('<I',context,16,0x800)
            memory=bytes([(i*37)&255])*4096
            target=targets[i%len(targets)]
            fpscr=(0,0x9f,0xc00010,0x300009f)[i%4]
            a=machine.run(bytes(context),memory,target,0,fpscr)
            b=machine.run(bytes(context),memory,target,1,fpscr)
            for field in ('context','memory','fpscr','marker','callbacks','hles','magic_calls'):
                assert a[field]==b[field],(i,hex(target),field)
            assert a['fpscr']==fpscr and a['marker']==0x1234
            rows.append(dict(target=target,off_instructions=a['instructions'],on_instructions=b['instructions'],
                             hits=b['hle_dispatch_hits'],stores=b['hle_dispatch_stores']))
        assert sum(r['hits'] for r in rows)>1000
        item=dict(traced=traced,comparisons=len(rows),elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                  command=command,rows=rows)
        report.append(item)
        print('PASS ARM dispatch trace',traced,len(rows),'full-context/arena/FPSCR/callback comparisons',flush=True)
        for target in targets:
            selected=[r for r in rows if r['target']==target]
            print(hex(target),'off',round(sum(r['off_instructions'] for r in selected)/len(selected),2),
                  'on',round(sum(r['on_instructions'] for r in selected)/len(selected),2),
                  'hits',sum(r['hits'] for r in selected),'stores',sum(r['stores'] for r in selected))
    (args.output_dir/'result.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':main()
