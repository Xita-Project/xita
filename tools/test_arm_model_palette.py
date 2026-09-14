#!/usr/bin/env python3
"""Compare Vita-compiled model batches in a Cortex-A9 instruction emulator.

Requires private original.c from test_model_palette.py --output-dir, VitaSDK,
Unicorn and pyelftools. Memory-copy imports are modeled and counted separately;
instruction counts are neither CPU cycles nor hardware frame times.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
    UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

ROOT = Path(__file__).resolve().parents[1]
RAM, PT, STACK, CTX, END, ENV = (0x20000000, 0x21000000, 0x22000000,
                                0x23000000, 0x24000000, 0x25000000)
SIZE = 1 << 20
FIELDS = ('size', 'r', 'st', 'fsp', 'fsw', 'fcw', 'preempt', 'f_kind', 'f_bits', 'xmm')


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
void __wrap_xv_preempt(xctx *c) { c->preempt = 100; }
void *memcpy(void *dest, const void *source, size_t size) { (void)source;(void)size;return dest; }
void *memset(void *dest, int value, size_t size) { (void)value;(void)size;return dest; }
void *memmove(void *dest, const void *source, size_t size) { (void)source;(void)size;return dest; }
''')
    elf = directory / 'arm-test.elf'
    common = [cc, '-O2', '-fno-strict-aliasing', '-ffp-contract=off', '-mthumb',
              '-mcpu=cortex-a9', '-mfpu=neon', '-std=gnu11', '-I' + str(ROOT / 'recomp'),
              '-DXV_NATIVE_MODEL_PALETTE', '-ffunction-sections', '-fdata-sections']
    commands, objects = [], []
    for index, source in enumerate((reference, harness, ROOT / 'recomp/kernel/xk_palette.c',
                                   ROOT / 'recomp/kernel/xk_math.c', ROOT / 'recomp/xv_x86rt.c')):
        obj = directory / f'unit-{index}.o'
        # Match the full game's per-unit optimization without changing the
        # independent original lift or surrounding guest runtime.
        flags = ['-O3', '-funroll-loops'] if source.name in ('xk_palette.c', 'xk_math.c') else []
        command = common + flags + ['-c', str(source), '-o', str(obj)]
        subprocess.run(command, check=True); commands.append(command); objects.append(str(obj))
    command = common + objects + ['-nostdlib',
        '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,'
        '--undefined=original_palette,--undefined=current_palette,--undefined=candidate_palette,'
        '--undefined=xv_math_model_palette,--undefined=layout', '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True); commands.append(command)
    return elf, commands



class Machine:
    def __init__(self, path, enabled="on"):
        self.enabled = enabled
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        uc.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
        uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        with path.open('rb') as file:
            elf = ELFFile(file)
            self.symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
            segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
            pages = sorted({page for segment in segments for page in range(
                segment['p_vaddr'] & ~4095, (segment['p_vaddr'] + segment['p_memsz'] + 4095) & ~4095, 4096)})
            for page in pages:
                uc.mem_map(page, 4096)
            for segment in segments:
                uc.mem_write(segment['p_vaddr'], segment.data())
        for base, size in ((RAM, SIZE), (PT, 4 << 20), (STACK, 65536),
                           (CTX, 4096), (END, 4096), (ENV, 4096)):
            uc.mem_map(base, size)
        uc.mem_write(ENV, b'1\0' + b'0\0')
        for name, value in (('g_xram', RAM), ('g_xpt', PT), ('g_img_base', RAM)):
            if name in self.symbols:
                uc.mem_write(self.symbols[name], struct.pack('<I', value))
        data = uc.mem_read(self.symbols['layout'], len(FIELDS) * 4)
        self.layout = dict(zip(FIELDS, struct.unpack('<' + 'I' * len(FIELDS), data)))
        self.imports = {self.symbols[n] & ~1: n for n in
                        ('getenv', 'atoi', 'xk_os_log', 'memcpy', 'memmove', 'memset', '__wrap_xv_preempt')
                        if n in self.symbols}
        uc.hook_add(UC_HOOK_CODE, self.step)

    def step(self, uc, address, size, user):
        self.instructions += 1
        name = self.imports.get(address)
        if not name:
            return
        if name == 'getenv':
            key = bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 64)).split(b'\0')[0]
            value = ENV
            if key == b'XV_NATIVE_MODEL_PALETTE':
                value = {'on': ENV, 'off': ENV + 2, 'unset': 0}[self.enabled]
            uc.reg_write(UC_ARM_REG_R0, value)
        elif name == 'atoi':
            value = uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 1)[0] - ord('0')
            uc.reg_write(UC_ARM_REG_R0, value)
        elif name == 'xk_os_log':
            pass
        elif name == '__wrap_xv_preempt':
            self.yields += 1
            return  # Execute the fixture's actual budget-reset instructions.
        else:
            dst, src, count = (uc.reg_read(reg) for reg in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            assert count <= SIZE
            data = bytes([src & 255]) * count if name == 'memset' else bytes(uc.mem_read(src, count))
            uc.mem_write(dst, data)
            self.copies += 1
            self.copy_bytes += count
            uc.reg_write(UC_ARM_REG_R0, dst)
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def run(self, function, fixture):
        memory, context, pages, fpscr = fixture
        uc = self.uc
        uc.mem_write(RAM, memory)
        uc.mem_write(CTX, context)
        uc.mem_write(PT, struct.pack('<' + 'I' * len(pages), *pages))
        uc.mem_write(STACK, bytes(65536))
        uc.reg_write(UC_ARM_REG_R0, CTX)
        uc.reg_write(UC_ARM_REG_SP, STACK + 65024)
        uc.reg_write(UC_ARM_REG_LR, END | 1)
        uc.reg_write(UC_ARM_REG_FPSCR, fpscr)
        self.instructions = self.copies = self.copy_bytes = self.yields = 0
        counters = ('palette_batches', 'math_fast', 'math_fallback')
        before = {name: struct.unpack('<I', uc.mem_read(self.symbols[name], 4))[0]
                  for name in counters}
        uc.emu_start(self.symbols[function] | 1, END, count=2000000)
        assert uc.reg_read(UC_ARM_REG_PC) == END, function + ' did not return'
        delta = {name: (struct.unpack('<I', uc.mem_read(self.symbols[name], 4))[0] - before[name]) & 0xffffffff
                 for name in counters}
        return dict(result=uc.reg_read(UC_ARM_REG_R0), fpscr=uc.reg_read(UC_ARM_REG_FPSCR), context=bytes(uc.mem_read(CTX, self.layout['size'])),
                    memory=bytes(uc.mem_read(RAM, SIZE)), instructions=self.instructions,
                    copies=self.copies, copy_bytes=self.copy_bytes, yields=self.yields,
                    batches=delta['palette_batches'], native_matrices=delta['math_fast'],
                    translated_matrices=delta['math_fallback'])


EDGES = [0, 0x80000000, 1, 0x807fffff, 0x00800000, 0x80800000,
         0x307fffff, 0x30800000, 0x30800001, 0x4e7fffff, 0x4e800000, 0x4e800001,
         0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc01234, 0xffc05678,
         0x7f801234, 0x3f800000, 0xbf800000, 0x3f000001, 0xbf000001]

def fixture(layout, count, variant, fpscr, edges=False):
    memory = bytearray(b'\xa5' * SIZE)
    context = bytearray(layout['size'])
    pages = [(i ^ 0x40) * 4096 for i in range(SIZE // 4096)]
    model, pose, nodes, sp = 0x12000, 0x21080, 0x31080, 0x51800
    if variant == 1:
        nodes = pose - 0x68
    elif variant == 2:
        pose = 0x21ffc
        pages[0x22] = pages[0x25]
    elif variant == 3:
        pages[sp >> 12] = pages[pose >> 12]
    elif variant == 5:
        model = 0x12ffc - 0xb8
    elif variant == 6:
        pose += 1
    elif variant == 7:
        sp = 0x51010  # Existing per-matrix stack guard falls back across this page.

    def write(address, data):
        for index, value in enumerate(data):
            memory[pages[(address + index) >> 12] + ((address + index) & 4095)] = value

    def word(address, value):
        write(address, struct.pack('<I', value))

    def field(name, value, fmt='I'):
        struct.pack_into('<' + fmt, context, layout[name], value)

    def reg(index, value):
        struct.pack_into('<I', context, layout['r'] + index * 4, value)

    for i in range(8):
        reg(i, 0x12345000 + i)
        struct.pack_into('<d', context, layout['st'] + i * 8, i + .375)
        for j in range(4):
            struct.pack_into('<f', context, layout['xmm'] + (i * 4 + j) * 4, i * 4 + j + .25)
    reg(4, sp); reg(5, model); reg(7, pose)
    field('fsp', count % 8); field('fsw', 0xabcd, 'H'); field('fcw', 0x37f, 'H')
    field('preempt', max(count - 1, 0) if variant == 4 else count)
    field('f_kind', 3); field('f_bits', 32)
    word(model + 0xb8, count); word(model + 0xbc, nodes)
    for n in range(count):
        for j in range(13):
            if edges:
                a = struct.pack('<I', EDGES[(n * 13 + j + variant * 7 + (fpscr >> 22)) % len(EDGES)])
                b = struct.pack('<I', EDGES[(n * 7 + j + variant * 11 + (fpscr >> 22) * 3) % len(EDGES)])
            else:
                a = struct.pack('<f', ((n * 13 + j) % 17 - 8) * .037)
                b = struct.pack('<f', ((n * 7 + j) % 13 - 6) * .021)
            write(pose + n * 52 + j * 4, a)
            write(nodes + 0x68 + n * 156 + j * 4, b)
    return bytes(memory), bytes(context), pages, fpscr


def write_word(sample, address, value):
    memory, context, pages, fpscr = sample
    memory = bytearray(memory)
    for i, byte in enumerate(struct.pack('<I', value)):
        memory[pages[(address + i) >> 12] + ((address + i) & 4095)] = byte
    return bytes(memory), context, pages, fpscr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('ARM_CC', 'arm-vita-eabi-gcc'))
    parser.add_argument('--values', choices=('finite', 'edges', 'boundaries'), default='finite')
    parser.add_argument('--enabled', choices=('on', 'off', 'unset'), default='on')
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    elf, commands = build(args.output_dir, args.reference, args.cc)
    machine = Machine(elf, args.enabled)
    for function in ('original_palette', 'current_palette', 'candidate_palette'):
        machine.run(function, fixture(machine.layout, 1, 0, 0))
    rows = []
    for rounding in range(4):
        cases = []
        if args.values == 'boundaries':
            # Each component, each side and both ends of a multi-node batch.
            for count in (1, 4):
                for node in sorted({0, count - 1}):
                    for component in range(26):
                        for edge in EDGES:
                            sample = fixture(machine.layout, count, 0, rounding << 22)
                            address = (0x21080 + node * 52 + component * 4 if component < 13 else
                                       0x31080 + 0x68 + node * 156 + (component - 13) * 4)
                            sample = write_word(sample, address, edge)
                            cases.append((dict(rounding=rounding, count=count, variant=0,
                                               node=node, component=component, edge=edge), sample))
        else:
            for control in (0, 0x10, 0x9f, 0x01000000, 0x02000000, 0x0300009f):
                for count in (0, 1, 2, 4, 8, 16, 32, 64, 65):
                    for variant in range(8):
                        fpscr = (rounding << 22) | control
                        cases.append((dict(rounding=rounding, control=control, count=count, variant=variant),
                                      fixture(machine.layout, count, variant, fpscr, args.values == 'edges')))
        for row, sample in cases:
            # A rejected batch must not make even a temporary guest mutation.
            admission = machine.run('xv_math_model_palette', sample)
            if not admission['result']:
                assert admission['context'] == sample[1] and admission['memory'] == sample[0]
                assert admission['fpscr'] == sample[3] and admission['yields'] == 0
            if args.enabled != 'on':
                assert not admission['result']
            results = {name: machine.run(name + '_palette', sample)
                       for name in ('original', 'current', 'candidate')}
            # Ordinary finite fixtures must match the original lift. For
            # exceptional values, retain the existing native leaf's established
            # NaN payload priority (which can differ from the generic lift).
            reference_name = 'original' if args.values == 'finite' else 'current'
            reference = results[reference_name]
            for name in ('current', 'candidate'):
                for field in ('context', 'memory', 'yields', 'fpscr'):
                    if results[name][field] != reference[field]:
                        diagnostic = args.output_dir / f'mismatch-{len(rows)}-{name}-{field}'
                        diagnostic.with_suffix('.json').write_text(json.dumps(row, indent=2))
                        if field in ('context', 'memory'):
                            diagnostic.with_suffix('.expected').write_bytes(reference[field])
                            diagnostic.with_suffix('.actual').write_bytes(results[name][field])
                        raise AssertionError(str(diagnostic))
            assert results['candidate']['batches'] in (0, 1)
            if admission['result']:
                for field in ('context', 'memory', 'yields', 'fpscr'):
                    assert results['candidate'][field] == results['original'][field], (row, field)
            if row['variant'] == 0 and 1 <= row['count'] <= 64:
                assert results['current']['native_matrices'] > 0
            row['admitted'] = admission['result']
            row.update({name: {k: v for k, v in result.items() if k not in ('context', 'memory')}
                        for name, result in results.items()})
            rows.append(row)
        print(f'PASS ARM rounding mode {rounding}: {len(rows)} fixture comparisons', flush=True)
    # Native exceptions are never unmasked by this experiment. Decline before
    # arithmetic if a caller has enabled any native exception trap.
    unsupported_controls = []
    for mask in (0x100, 0x200, 0x400, 0x800, 0x1000, 0x8000):
        machine.uc.reg_write(UC_ARM_REG_FPSCR, mask)
        if machine.uc.reg_read(UC_ARM_REG_FPSCR) != mask:
            # This Cortex-A9 model treats native trap enables as read-as-zero.
            # Do not count a requested-but-ignored mode as a tested decline.
            unsupported_controls.append(mask)
            continue
        sample = fixture(machine.layout, 4, 0, mask)
        admission = machine.run('xv_math_model_palette', sample)
        assert not admission['result'] and admission['fpscr'] == mask
        assert admission['context'] == sample[1] and admission['memory'] == sample[0]
    report = dict(fixtures=len(rows), values=args.values, enabled=args.enabled,
                  full_context_and_arena_match=True, fpscr_match=True,
                  unsupported_native_exception_controls=unsupported_controls,
                  limitation='Instruction counts exclude modeled memory-copy bodies; not cycles or Vita FPS',
                  elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), commands=commands, rows=rows)
    (args.output_dir / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    for row in rows:
        if args.values != 'boundaries' and row['rounding'] == row['variant'] == row.get('control', 0) == 0:
            print('ARM nodes', row['count'], 'current', row['current']['instructions'],
                  'batch', row['candidate']['instructions'], 'batch accepted', row['candidate']['batches'])


if __name__ == '__main__':
    main()
