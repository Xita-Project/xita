#!/usr/bin/env python3
"""Compare two actual Vita-linked math implementations after a runtime change.

Uses synthetic finite polygon fixtures, including aliases and split guest pages.
Requires Unicorn/pyelftools and VitaSDK. Firmware memory copies are modeled;
libgcc executes normally. Instruction counts are not CPU cycles or game FPS.
The full guest context and 2 MiB guest arena must match byte for byte.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

import unicorn
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
    UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--candidate', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
fields = ['r', 'st', 'fsp', 'fsw', 'fcw', 'preempt', 'f_kind', 'f_bits']
meta, obj = out / 'layout.c', out / 'layout.o'
meta.write_text('#include <stddef.h>\n#include "xv_x86rt.h"\nconst unsigned layout[] = {sizeof(xctx),'
                + ','.join('offsetof(xctx,' + f + ')' for f in fields) + '};\n')
subprocess.run(['arm-vita-eabi-gcc', '-O2', '-I' + str(root / 'recomp'),
                '-c', str(meta), '-o', str(obj)], check=True)
with obj.open('rb') as file:
    elf = ELFFile(file)
    symbol = elf.get_section_by_name('.symtab').get_symbol_by_name('layout')[0]
    data = elf.get_section(symbol['st_shndx']).data()[symbol['st_value']:symbol['st_value'] + symbol['st_size']]
    layout = dict(zip(['size'] + fields, struct.unpack('<' + 'I' * (len(fields) + 1), data)))

RAM, PT, STACK, CTX, END = 0x20000000, 0x21000000, 0x22000000, 0x23000000, 0x24000000
SIZE = 2 << 20
pages = [(i ^ 1) * 4096 for i in range(SIZE // 4096)]
pages[0x38], pages[0x39] = pages[0x18], pages[0x19]
page_table = struct.pack('<' + 'I' * len(pages), *pages)


class Machine:
    def __init__(self, path):
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        uc.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
        uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        with path.open('rb') as file:
            elf = ELFFile(file)
            self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
            segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
            ranges = []
            for segment in sorted(segments, key=lambda s: s['p_vaddr']):
                base, size = segment['p_vaddr'], segment['p_memsz']
                lo, hi = base & ~4095, (base + size + 4095) & ~4095
                if ranges and lo <= ranges[-1][1]:
                    ranges[-1][1] = max(hi, ranges[-1][1])
                else:
                    ranges.append([lo, hi])
            # Coalesce pages before mapping. Thousands of separate regions
            # made Unicorn teardown far more expensive than the comparison.
            for lo, hi in ranges:
                uc.mem_map(lo, hi - lo)
            for segment in segments:
                uc.mem_write(segment['p_vaddr'], segment.data())
        for base, size in [(RAM, SIZE), (PT, 4 << 20), (STACK, 65536), (CTX, 4096), (END, 4096)]:
            uc.mem_map(base, size)
        uc.mem_write(PT, page_table)
        for name, value in [('g_xram', RAM), ('g_xpt', PT), ('g_img_base', RAM)]:
            uc.mem_write(self.symbols[name], struct.pack('<I', value))
        self.imports = {self.symbols[n] & ~1: n for n in
                        ('getenv', 'sceClibMemcpy', 'sceClibMemmove', 'sceClibMemset', 'xv_preempt')}
        self.count = self.copy_calls = self.copy_bytes = 0
        uc.hook_add(UC_HOOK_CODE, self.step)

    def step(self, uc, address, size, user):
        self.count += 1
        name = self.imports.get(address)
        if not name:
            return
        assert name != 'xv_preempt', 'fixture exhausted scheduling budget'
        if name == 'getenv':
            uc.reg_write(UC_ARM_REG_R0, 0)
        else:
            dst, src, count = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            assert count <= SIZE
            content = bytes([src & 255]) * count if name == 'sceClibMemset' else bytes(uc.mem_read(src, count))
            uc.mem_write(dst, content)
            self.copy_calls += 1
            self.copy_bytes += count
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def call(self, name, memory, context):
        self.uc.mem_write(RAM, memory)
        self.uc.mem_write(CTX, context)
        self.uc.reg_write(UC_ARM_REG_R0, CTX)
        self.uc.reg_write(UC_ARM_REG_SP, STACK + 65024)
        self.uc.reg_write(UC_ARM_REG_LR, END | 1)
        self.count = self.copy_calls = self.copy_bytes = 0
        self.uc.emu_start(self.symbols[name] | 1, END, count=1000000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == END, name + ' did not return'
        return (bytes(self.uc.mem_read(CTX, layout['size'])), bytes(self.uc.mem_read(RAM, SIZE)),
                self.count, (self.copy_calls, self.copy_bytes))


def fixture(name, k):
    memory = bytearray(b'\xa5' * SIZE)
    context = bytearray(layout['size'])

    def write(a, data):
        for i, value in enumerate(data):
            memory[pages[(a + i) >> 12] + ((a + i) & 4095)] = value

    def word(a, value):
        write(a, struct.pack('<I', value))

    def fl(a, value):
        write(a, struct.pack('<f', value))

    def field(name, value, fmt='I'):
        struct.pack_into('<' + fmt, context, layout[name], value)

    def reg(n, value):
        struct.pack_into('<I', context, layout['r'] + n * 4, value)

    count = [0, 1, 2, 3, 4, 8, 16, 64][k % 8]
    sp, source, output, parameter = 0x62000, 0x18ffc + k % 4, 0x21000 + k % 4, 0x31ffc + k % 4
    if k % 5 == 0:
        output = source
    elif k % 5 == 1:
        output = source + 4
    elif k % 5 == 2:
        output = source + 0x20000  # separate guest pages alias the same physical pages
    for i in range(8):
        reg(i, 0x10000000 + i)
        struct.pack_into('<d', context, layout['st'] + i * 8, i + .375)
    reg(4, sp)
    field('fsp', k % 8)
    field('fsw', 0xabcd, 'H')
    field('fcw', 0x37f, 'H')
    field('preempt', 100000)
    field('f_kind', 3)
    field('f_bits', 32)
    fl(0x1f0a68, 0)
    fl(0x1f0a78, 1)
    word(sp, 0x12345678)
    for i in range(count):
        angle = (1 if k % 3 else -1) * i * 2 * math.pi / max(count, 1)
        fl(source + i * 8, math.cos(angle) * 3)
        fl(source + i * 8 + 4, math.sin(angle) * 3)
    if name == 'f_000B77C0':
        reg(1, source)
        word(sp + 4, count)
        word(sp + 8, parameter)
        fl(sp + 12, [0, .01, 1, -1][k % 4])
        fl(parameter, (k // 8 % 4) * 1.5)
        fl(parameter + 4, (k // 16 % 3) * .75)
    else:
        reg(2, output)
        word(sp + 4, count)
        word(sp + 8, source)
        word(sp + 12, parameter)
        word(sp + 16, count + 2 if k % 3 else k % 7)
        word(sp + 20, 0x43000 if k % 2 else 0)
        word(sp + 24, 0x44000 if k % 3 else 0)
        fl(sp + 28, [0, .001, -.001][k % 3])
        fl(parameter, 1 if k % 3 else 0)
        fl(parameter + 4, 1 if k % 3 == 0 else 0)
        fl(parameter + 8, (k // 8 % 7 - 3) * .5)
    return bytes(memory), bytes(context)


baseline, candidate = Machine(args.baseline), Machine(args.candidate)
report = {}
for name in ('f_000B77C0', 'xv_math_polygon_clip'):
    rows = []
    # Warm up lazy environment lookups equally before measuring.
    memory, context = fixture(name, 0)
    baseline.call(name, memory, context)
    candidate.call(name, memory, context)
    for k in range(128):
        memory, context = fixture(name, k)
        old, new = baseline.call(name, memory, context), candidate.call(name, memory, context)
        assert old[0] == new[0], (name, k, 'context')
        assert old[1] == new[1], (name, k, 'guest memory')
        assert old[3] == new[3], (name, k, 'firmware copies')
        rows.append({'case': k, 'baseline': old[2], 'candidate': new[2]})
    before, after = (sum(r[n] for r in rows) for n in ('baseline', 'candidate'))
    report[name] = {'cases': len(rows), 'baseline_instructions': before,
                    'candidate_instructions': after, 'change_percent': (after / before - 1) * 100,
                    'faster_cases': sum(r['candidate'] < r['baseline'] for r in rows),
                    'slower_cases': sum(r['candidate'] > r['baseline'] for r in rows)}
    (out / (name + '.json')).write_text(json.dumps(rows, indent=2) + '\n')
report = {'unicorn': unicorn.__version__, 'results': report, 'units': 'instructions, not cycles or FPS',
          'inputs': {n: {'path': str(p.resolve()), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                     for n, p in [('baseline', args.baseline), ('candidate', args.candidate)]}}
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
