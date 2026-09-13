#!/usr/bin/env python3
"""Check Vita-compiled basis preparation against the original lift on Cortex-A9.

Uses the existing ARM fixture loader; imported memory-copy bodies are modeled,
so instruction counts are not cycles or hardware FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess

from test_arm_model_palette import Machine, RAM, PT, STACK, CTX, END, SIZE, FIELDS
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR

ROOT = Path(__file__).resolve().parents[1]


def build(directory, reference, cc):
    harness = directory / 'arm-fixture.c'
    harness.write_text('''#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
const unsigned layout[] = {sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void test_boot(void) {}
void xk_os_log(const char *format, ...) { (void)format; }
char *getenv(const char *name) { (void)name; return (char *)0; }
int atoi(const char *value) { (void)value; return 0; }
void *memcpy(void *dest, const void *source, size_t size) { (void)source;(void)size;return dest; }
void *memset(void *dest, int value, size_t size) { (void)value;(void)size;return dest; }
void *memmove(void *dest, const void *source, size_t size) { (void)source;(void)size;return dest; }
''')
    elf = directory / 'arm-test.elf'
    command = [cc, '-O2', '-fno-strict-aliasing', '-ffp-contract=off', '-mthumb',
               '-mcpu=cortex-a9', '-mfpu=neon', '-std=gnu11', '-I' + str(ROOT / 'recomp'),
               '-DXV_NATIVE_OBJECT_BASIS', '-ffunction-sections', '-fdata-sections',
               str(reference), str(harness), str(ROOT / 'recomp/kernel/xk_object_basis.c'),
               str(ROOT / 'recomp/xv_x86rt.c'), '-nostdlib',
               '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--undefined=original_basis,'
               '--undefined=candidate_basis,--undefined=layout', '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True)
    return elf, command


class BasisMachine(Machine):
    def run(self, function, fixture):
        memory, context, pages, controls = fixture
        uc = self.uc
        uc.mem_write(RAM, memory); uc.mem_write(CTX, context)
        uc.mem_write(PT, struct.pack('<' + 'I' * len(pages), *pages))
        uc.mem_write(STACK, bytes(65536))
        uc.reg_write(UC_ARM_REG_R0, CTX)
        uc.reg_write(UC_ARM_REG_SP, STACK + 65024)
        uc.reg_write(UC_ARM_REG_LR, END | 1)
        uc.reg_write(UC_ARM_REG_FPSCR, controls)
        self.instructions = self.copies = self.copy_bytes = self.yields = 0
        before = struct.unpack('<I', uc.mem_read(self.symbols['basis_used'], 4))[0]
        uc.emu_start(self.symbols[function] | 1, END, count=100000)
        assert uc.reg_read(UC_ARM_REG_PC) == END
        after = struct.unpack('<I', uc.mem_read(self.symbols['basis_used'], 4))[0]
        return dict(context=bytes(uc.mem_read(CTX, self.layout['size'])),
                    memory=bytes(uc.mem_read(RAM, SIZE)), instructions=self.instructions,
                    copies=self.copies, copy_bytes=self.copy_bytes, accepted=(after-before)&0xffffffff,
                    fpscr=uc.reg_read(UC_ARM_REG_FPSCR))


def fixture(layout, case, controls):
    rng = random.Random(7456 + case)
    memory = bytearray(b'\xa5' * SIZE)
    context = bytearray(layout['size'])
    pages = [(i ^ 0x40) * 4096 for i in range(SIZE // 4096)]
    obj, sp = 0x21080, 0x51800
    variant = case // 16
    if variant == 1:
        obj = 0x21fe0; pages[0x22] = pages[0x25]
    elif variant == 2:
        obj = sp + 0x40
    elif variant == 3:
        obj = 0x21840; pages[0x21] = pages[0x51]
    elif variant == 4:
        sp = 0x51fa0; pages[0x52] = pages[0x55]
    elif variant == 5:
        # Accepted contiguous page crossing for both spans.
        obj = 0x21fe0; sp = 0x51fa0

    def write(address, data):
        for i, value in enumerate(data):
            memory[pages[(address+i)>>12]+((address+i)&4095)] = value
    def field(name, value, fmt='I'):
        struct.pack_into('<'+fmt, context, layout[name], value)
    for i in range(8):
        struct.pack_into('<I', context, layout['r']+i*4, rng.getrandbits(32))
        struct.pack_into('<d', context, layout['st']+i*8, i+.375)
        for j in range(4):
            struct.pack_into('<f', context, layout['xmm']+(i*4+j)*4, i*4+j+.25)
    struct.pack_into('<I', context, layout['r']+4*4, sp)
    struct.pack_into('<I', context, layout['r']+5*4, obj)
    field('fsp',case%8); field('fsw',0xabcd,'H'); field('fcw',0x37f,'H')
    field('preempt',rng.getrandbits(32)); field('f_kind',3); field('f_bits',32)
    edges = [0,0x80000000,1,0x807fffff,0x800000,0x7f7fffff,0xff7fffff,
             0x7f800000,0xff800000,0x7fc01234,0x7f801234,0x3f800000]
    for i in range(14):
        word = rng.getrandbits(32)
        if case%3 == 0:
            word = (word&0x807fffff)|((110+case%30)<<23)
        if case%3 == 1:
            word = edges[(case+i)%len(edges)]
        if i == 0:
            word = (word&~0x1000)|((case&1)<<12)
        write(obj+4+i*4, struct.pack('<I', word))
    return bytes(memory), bytes(context), pages, controls


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('ARM_CC','arm-vita-eabi-gcc'))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    elf, command = build(args.output_dir,args.reference,args.cc)
    machine = BasisMachine(elf)
    machine.run('candidate_basis',fixture(machine.layout,0,0))
    rows = []
    for mode in range(16):
        controls = mode << 22  # All rounding modes, FZ and DN combinations.
        for case in range(96):
            sample = fixture(machine.layout,case,controls)
            results = {name:machine.run(name+'_basis',sample) for name in ('original','candidate')}
            for field in ('context','memory','fpscr'):
                if results['original'][field] != results['candidate'][field]:
                    path = args.output_dir/f'mismatch-{mode}-{case}-{field}'
                    if field != 'fpscr':
                        path.with_suffix('.expected').write_bytes(results['original'][field])
                        path.with_suffix('.actual').write_bytes(results['candidate'][field])
                    raise AssertionError((str(path),results['original']['fpscr'],results['candidate']['fpscr']))
            assert results['candidate']['accepted'] == (case//16 in (0,5))
            rows.append(dict(mode=mode,case=case,**{name:{k:v for k,v in result.items()
                        if k not in ('context','memory')} for name,result in results.items()}))
        print(f'PASS ARM FP mode {mode}: {len(rows)} full-state comparisons',flush=True)
    report = dict(fixtures=len(rows),full_context_and_arena_match=True,native_fp_status_match=True,
                  limitation='Instruction counts exclude modeled memory-copy bodies; not cycles or Vita FPS',
                  elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),command=command,rows=rows)
    (args.output_dir/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(rows[0]))


if __name__ == '__main__':
    main()
