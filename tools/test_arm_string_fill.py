#!/usr/bin/env python3
"""Execute old/new Vita string fills on Cortex-A9; memory imports are modeled.

Requires Unicorn, pyelftools and VitaSDK. Counts are instructions, not cycles.
Pass a saved previous recomp/xv_x86rt.c as --baseline-runtime.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from test_arm_model_palette import Machine, RAM, PT, STACK, CTX, END, SIZE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    begin = source.index(signature)
    end = source.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end] + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-runtime', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('ARM_CC', 'arm-vita-eabi-gcc'))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    current = (ROOT / 'recomp/xv_x86rt.c').read_text()
    previous = args.baseline_runtime.read_text()
    source = '''#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram;
uint32_t *g_xpt;
const unsigned layout[] = {sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
const unsigned df_offset = offsetof(xctx,df);
void test_boot(void) {}
/* The memory-watch logger is unrelated to the fill and disabled in this fixture. */
static void watch_range(xctx *c,const char *op,uint32_t dst,uint32_t n)
{ (void)c;(void)op;(void)dst;(void)n; }
#define STEP(sz) (c->df ? 0u-(sz) : (sz))
'''
    source += function(current, 'void x_guest_write_pages(')
    source += function(current, 'static inline void st(')
    source += function(previous, 'void x_str_stos(').replace('void x_str_stos(', 'void old_stos(', 1)
    source += function(current, 'void x_str_stos(')
    fixture = args.output_dir / 'fixture.c'
    fixture.write_text(source)
    imports = args.output_dir / 'imports.c'
    imports.write_text('''#include <stddef.h>
void *memcpy(void *d,const void *s,size_t n) { (void)s;(void)n;return d; }
void *memset(void *d,int v,size_t n) { (void)v;(void)n;return d; }
''')
    elf = args.output_dir / 'fixture.elf'
    command = [args.cc, '-O2', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon',
               '-std=gnu11', '-fno-strict-aliasing', '-ffunction-sections', '-fdata-sections',
               '-I' + str(ROOT / 'recomp'), str(fixture), str(imports), '-nostdlib',
               '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--undefined=old_stos,'
               '--undefined=x_str_stos,--undefined=layout,--undefined=df_offset', '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True)
    machine = Machine(elf)
    uc = machine.uc
    pages = [(i * 3 % 7) * 4096 for i in range(1 << 20)]
    uc.mem_write(PT, struct.pack('<' + 'I' * len(pages), *pages))
    df_offset = struct.unpack('<I', uc.mem_read(machine.symbols['df_offset'], 4))[0]
    rows = []
    cases = 0

    def case(sz, df, mode, value, address, count, record=False):
        nonlocal cases
        memory = bytes([0xA5]) * SIZE
        context = bytearray([0xA5] * machine.layout['size'])
        struct.pack_into('<I', context, df_offset, df)
        for reg, word in [(0, value), (1, count), (7, address)]:
            struct.pack_into('<I', context, machine.layout['r'] + reg * 4, word)
        expected = bytearray(memory)
        final_address = address
        for _ in range(1 if mode == 0 else count):
            for b in range(sz):
                a = (final_address + b) & 0xffffffff
                expected[pages[a >> 12] + (a & 4095)] = (value >> (8 * b)) & 255
            final_address = (final_address + (-sz if df else sz)) & 0xffffffff
        want = bytearray(context)
        struct.pack_into('<I', want, machine.layout['r'] + 7 * 4, final_address)
        if mode != 0:
            struct.pack_into('<I', want, machine.layout['r'] + 4, 0)
        result = {}
        for name in ('old_stos', 'x_str_stos'):
            uc.mem_write(RAM, memory); uc.mem_write(CTX, bytes(context))
            uc.mem_write(STACK, bytes(65536))
            for reg, word in [(UC_ARM_REG_R0, CTX), (UC_ARM_REG_R1, sz), (UC_ARM_REG_R2, mode),
                              (UC_ARM_REG_SP, STACK + 65024), (UC_ARM_REG_LR, END | 1)]:
                uc.reg_write(reg, word)
            machine.instructions = machine.copies = machine.copy_bytes = machine.yields = 0
            uc.emu_start(machine.symbols[name] | 1, END, count=2000000)
            assert uc.reg_read(UC_ARM_REG_PC) == END
            assert bytes(uc.mem_read(CTX, len(want))) == bytes(want), (name, cases, 'context')
            assert bytes(uc.mem_read(RAM, SIZE)) == bytes(expected), (name, cases, 'memory')
            result[name] = {'instructions': machine.instructions, 'modeled_copy_calls': machine.copies,
                            'modeled_copy_bytes': machine.copy_bytes}
        cases += 1
        if record:
            rows.append(dict(size=sz, df=df, mode=mode, value=hex(value), address=hex(address), count=count, **result))

    counts = [0, 1, 2, 7, 150, 1025]
    for sz in (1, 2, 4):
        for df in (0, 1):
            for mode in range(4):
                for value in (0, 0xffffffff, 0xA5A5A5A5, 0x1234AAAA, 0x12345678):
                    for address in (0x10001, 0x10ffd, 0x10fff, 0xfffffffd):
                        case(sz, df, mode, value, address, counts[cases % len(counts)])
    for sz in (1, 2, 4):
        for count in (1, 14, 150, 1024):
            for value in (0, 0x12345678):
                case(sz, 0, 1, value, 0x10000, count, True)
    report = dict(cases=cases, command=command, instruction_counts_not_cycles=rows,
                  current_sha256=hashlib.sha256(current.encode()).hexdigest(),
                  baseline_sha256=hashlib.sha256(previous.encode()).hexdigest(),
                  limitation='Watch logging disabled; imported memcpy/memset bodies modeled and counted separately.')
    (args.output_dir / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {cases} Cortex-A9 old/new fills, complete context and memory; see results.json for counts')


if __name__ == '__main__':
    main()
