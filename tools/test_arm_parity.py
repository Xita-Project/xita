#!/usr/bin/env python3
"""Compare Vita-compiled guest parity against the previous runtime expression.

Requires VitaSDK, Unicorn and pyelftools. Executes libgcc's real population
count too; counts instructions, not cycles, cache behavior or game FPS.
No game data, firmware imports, or intercepted memory accesses are needed.
"""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess

import unicorn
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0,
                              UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
source, binary = out / 'parity.c', out / 'parity.elf'
source.write_text('''#include <stddef.h>
#include "xv_x86rt.h"
const unsigned layout[] = {sizeof(xctx), offsetof(xctx,f_kind), offsetof(xctx,f_res)};
unsigned candidate(const xctx *c) { return XF_P(c); }
unsigned baseline(const xctx *c) {
    if (c->f_kind == XK_EXPLICIT) return (c->f_res >> 2) & 1u;
    return (__builtin_popcount(c->f_res & 0xffu) & 1u) == 0;
}
''')
subprocess.run(['arm-vita-eabi-gcc', '-O2', '-mthumb', '-mcpu=cortex-a9',
                '-mfpu=neon', '-std=gnu11', '-fno-strict-aliasing',
                '-I' + str(root / 'recomp'), '-nostdlib', '-Wl,-e,candidate',
                '-Wl,-Ttext=0x10000', str(source), '-lgcc', '-o', str(binary)],
               check=True)
uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
with binary.open('rb') as file:
    elf = ELFFile(file)
    symtab = elf.get_section_by_name('.symtab')
    symbols = {s.name: s['st_value'] for s in symtab.iter_symbols() if s.name}
    pages = set()
    for segment in elf.iter_segments():
        if segment['p_type'] != 'PT_LOAD':
            continue
        lo, size = segment['p_vaddr'], segment['p_memsz']
        for page in range(lo & ~4095, (lo + size + 4095) & ~4095, 4096):
            if page not in pages:
                uc.mem_map(page, 4096)
                pages.add(page)
        uc.mem_write(lo, segment.data())
    item = symtab.get_symbol_by_name('layout')[0]
    section = elf.get_section(item['st_shndx'])
    offset = item['st_value'] - section['sh_addr']
    ctx_size, kind_offset, value_offset = struct.unpack_from('<III', section.data(), offset)

CTX, STACK, END = 0x200000, 0x300000, 0x400000
for address, size in [(CTX, 4096), (STACK, 65536), (END, 4096)]:
    uc.mem_map(address, size)
instruction_count = 0


def step(emu, address, size, user):
    global instruction_count
    instruction_count += 1


uc.hook_add(UC_HOOK_CODE, step)


def call(name, context):
    global instruction_count
    uc.mem_write(CTX, context)
    uc.reg_write(UC_ARM_REG_R0, CTX)
    uc.reg_write(UC_ARM_REG_SP, STACK + 65536)
    uc.reg_write(UC_ARM_REG_LR, END | 1)
    instruction_count = 0
    uc.emu_start(symbols[name] | 1, END, count=200)
    assert uc.reg_read(UC_ARM_REG_PC) == END, name
    assert bytes(uc.mem_read(CTX, ctx_size)) == context, name + ' changed context'
    return uc.reg_read(UC_ARM_REG_R0), instruction_count


rng = random.Random(0x9669)
values = [(upper | low) for low in range(256)
          for upper in (0, 0xffffff00, 0xaaaaaa00, 0x55555500)]
values += [rng.getrandbits(32) for _ in range(4096)]
counts = {'lazy': {'baseline': [], 'candidate': []},
          'explicit': {'baseline': [], 'candidate': []}}
cases = 0
for kind in range(6):
    category = counts['explicit' if kind == 5 else 'lazy']
    for value in values:
        context = bytearray(b'\xa5' * ctx_size)
        struct.pack_into('<I', context, kind_offset, kind)
        struct.pack_into('<I', context, value_offset, value)
        expected = (value >> 2) & 1 if kind == 5 else int((value & 255).bit_count() % 2 == 0)
        for name in ('baseline', 'candidate'):
            result, instructions = call(name, bytes(context))
            assert result == expected, (name, kind, hex(value), result, expected)
            category[name].append(instructions)
        cases += 1

summary = {'cases': cases, 'calls': cases * 2, 'unicorn': unicorn.__version__,
           'units': 'ARM instructions, including the real linked libgcc helper; not cycles or FPS',
           'instructions': {group: {name: {'min': min(a), 'max': max(a),
                                           'mean': sum(a) / len(a)}
                                     for name, a in entries.items()}
                            for group, entries in counts.items()}}
(out / 'result.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
