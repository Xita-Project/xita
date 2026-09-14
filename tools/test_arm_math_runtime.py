#!/usr/bin/env python3
"""Compare two actual Vita-linked math implementations after a runtime change.

Uses synthetic fixtures, including aliases and split guest pages. Optional math
edge cases cover exceptional floats and caller-selected native FP controls.
Requires Unicorn/pyelftools and VitaSDK. Firmware memory copies are modeled;
libgcc executes normally. Instruction counts are not CPU cycles or game FPS.
The full guest context and 2 MiB guest arena must match byte for byte.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import subprocess

import unicorn
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
    UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_FPSCR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline', type=Path, required=True)
parser.add_argument('--candidate', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
parser.add_argument('--cases', type=int, default=128)
parser.add_argument('--float-edges', action='store_true')
parser.add_argument('--random-floats', action='store_true')
parser.add_argument('--bounded-matrices', action='store_true',
                    help='bounded finite matrix inputs with expanded alias/page layouts')
parser.add_argument('--matrix-boundaries', action='store_true',
                    help='matrix numeric guard boundaries at every input component')
parser.add_argument('--fpscr', type=lambda s: int(s, 0), nargs='+', default=[0])
parser.add_argument('--native-matrix', choices=['default', 'off', 'on'],
                    help='set the candidate compiled NEON matrix override')
parser.add_argument('--functions', nargs='+',
                    choices=['f_000B77C0', 'xv_math_polygon_clip', 'f_000B71C0',
                             'f_000B5B40', 'f_000B5F60', 'f_000B5EA0'],
                    default=['f_000B77C0', 'xv_math_polygon_clip'])
args = parser.parse_args()
if not 1 <= args.cases <= 10000:
    parser.error('--cases must be between 1 and 10000')
if any(value & ~0x03c0009f for value in args.fpscr):
    parser.error('--fpscr permits rounding, flush-to-zero, default-NaN and cumulative exception flags only')
if args.native_matrix and 'f_000B5B40' not in args.functions:
    parser.error('--native-matrix requires f_000B5B40 in --functions')
if sum((args.float_edges, args.random_floats, args.bounded_matrices,
        args.matrix_boundaries)) > 1:
    parser.error('choose one floating-point fixture mode')
if (args.bounded_matrices or args.matrix_boundaries) and args.functions != ['f_000B5B40']:
    parser.error('matrix-specific fixtures require only f_000B5B40')
if (args.float_edges or args.random_floats) and 'f_000B77C0' in args.functions:
    parser.error('float stress fixtures are not implemented for f_000B77C0')
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
        # Normal startup has resolved an absent XV_WATCH_FN. Without this,
        # an untraced direct-entry test leaves watch_n at its lazy -1 sentinel
        # and measures repeated no-op watch calls that normal startup avoids.
        uc.mem_write(self.symbols['xv_watch_n'], struct.pack('<I', 0))
        self.guest_trace_enabled = struct.unpack('<I', uc.mem_read(
            self.symbols['xv_guest_trace_enabled'], 4))[0]
        self.imports = {self.symbols[n] & ~1: n for n in
                        ('getenv', 'xk_os_log', 'sceClibMemcpy', 'sceClibMemmove', 'sceClibMemset', 'xv_preempt')}
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
        elif name == 'xk_os_log':
            pass  # Native math announces its setting once, during unmeasured warmup.
        else:
            dst, src, count = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            assert count <= SIZE
            content = bytes([src & 255]) * count if name == 'sceClibMemset' else bytes(uc.mem_read(src, count))
            uc.mem_write(dst, content)
            self.copy_calls += 1
            self.copy_bytes += count
        uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))

    def call(self, name, memory, context, fpscr=0):
        def matrix_counts():
            names = ('matrix_neon_accepted', 'matrix_neon_disabled',
                     'matrix_neon_fp', 'matrix_neon_numeric')
            return tuple(struct.unpack('<I', self.uc.mem_read(self.symbols[n], 4))[0]
                         if n in self.symbols else 0 for n in names)
        matrix_before = matrix_counts()
        def point_counts():
            if 'point_fast' not in self.symbols: return (0,0,0,0,0)
            return struct.unpack('<I', self.uc.mem_read(self.symbols['point_fast'],4)) + struct.unpack('<IIII', self.uc.mem_read(self.symbols['point_fallback'],16))
        point_before=point_counts()
        def math_counts():
            return sum((struct.unpack('<II', self.uc.mem_read(self.symbols[n], 8))
                        for n in ('math_fast', 'math_fallback')), ())
        before = math_counts()
        self.uc.mem_write(RAM, memory)
        self.uc.mem_write(CTX, context)
        self.uc.reg_write(UC_ARM_REG_R0, CTX)
        self.uc.reg_write(UC_ARM_REG_SP, STACK + 65024)
        self.uc.reg_write(UC_ARM_REG_LR, END | 1)
        self.uc.reg_write(UC_ARM_REG_FPSCR, fpscr)
        self.count = self.copy_calls = self.copy_bytes = 0
        self.uc.emu_start(self.symbols[name] | 1, END, count=1000000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == END, name + ' did not return'
        return (bytes(self.uc.mem_read(CTX, layout['size'])), bytes(self.uc.mem_read(RAM, SIZE)),
                self.count, (self.copy_calls, self.copy_bytes),
                tuple((a - b) & 0xffffffff for a, b in zip(math_counts(), before)),
                self.uc.reg_read(UC_ARM_REG_FPSCR),
                tuple((a-b)&0xffffffff for a,b in zip(point_counts(),point_before)),
                tuple((a-b)&0xffffffff for a,b in zip(matrix_counts(),matrix_before)))

    def matrix_mode(self, mode):
        symbol = self.symbols.get('xv_matrix_neon_override')
        if not symbol:
            raise RuntimeError('Candidate was not built with XV_NATIVE_MATRIX_NEON=1')
        self.uc.reg_write(UC_ARM_REG_R0, {'default': 0xffffffff, 'off': 0, 'on': 1}[mode])
        self.uc.reg_write(UC_ARM_REG_SP, STACK + 65024)
        self.uc.reg_write(UC_ARM_REG_LR, END | 1)
        self.uc.emu_start(symbol | 1, END, count=1000)
        assert self.uc.reg_read(UC_ARM_REG_PC) == END, 'matrix override did not return'


def fixture(name, k):
    memory = bytearray(b'\xa5' * SIZE)
    context = bytearray(layout['size'])
    rng = random.Random(0x5b5f60 + k)
    edges = [0, 0x80000000, 1, 0x807fffff, 0x00800000, 0x80800000,
             0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc01234,
             0xffc05678, 0x7f801234, 0x3f800000, 0xbf800000, 0x3f000001,
             0xbf000001]

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
    if name in ('f_000B5B40', 'f_000B5F60') and k % 11:
        sp = 0x62100  # include native fast paths as well as stack-page fallback
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
    elif name in ('xv_math_polygon_clip', 'f_000B71C0'):
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
        if args.float_edges or args.random_floats:
            # Keep control/count fields valid while stressing every numeric
            # input. Retain the ordinary alias, alignment and page layouts.
            # Rotate separate vertex, plane, epsilon and combined cases so
            # an exceptional plane cannot hide all vertex arithmetic.
            groups = [[source + i * 4 for i in range(count * 2)],
                      [parameter + i * 4 for i in range(3)], [sp + 28]]
            for group, addresses in enumerate(groups):
                if k % 4 != 3 and group != k % 4:
                    continue
                for i, address in enumerate(addresses):
                    bits = (rng.getrandbits(32) if args.random_floats else
                            edges[(k // 4 + group * 5 + i * 3) % len(edges)])
                    word(address, bits)
    elif name == 'f_000B5F60':
        source = 0x18000 + k % 4
        output = 0x22000 if k % 7 else source
        angle = k * .125
        for i, value in enumerate([math.sin(angle), .125, -.25, math.cos(angle)]):
            if args.random_floats: word(source + i * 4, rng.getrandbits(32))
            elif args.float_edges: word(source + i * 4, edges[(k // 4 + 3 * i) % len(edges)])
            else: fl(source + i * 4, value)
        fl(0x1f0b04, 2)
        reg(1, source)
        reg(2, output)
    elif name == 'f_000B5EA0':
        source, matrix, output = 0x18000, 0x1a000, 0x22000
        variant=k%18
        if variant==1: output=source
        elif variant==2: output=source+4
        elif variant==3: output=source-4
        elif variant==4: output=source+0x20000
        elif variant==5: output=matrix+48
        elif variant==6: source=matrix+16
        elif variant==7: matrix+=1
        elif variant==8: source+=1
        elif variant==9: output+=1
        elif variant==10: matrix+=4092
        elif variant==11: source+=4092
        elif variant==12: output+=4092
        elif variant==13: output=source+0x20004
        elif variant==14: output=sp
        elif variant==15: field('fsp',8+(k&7))
        elif variant==16: output=matrix
        elif variant==17: output=source+8
        for address, length, shift in ((matrix,13,0),(source,3,5)):
            for i in range(length):
                if args.random_floats: word(address+4*i,rng.getrandbits(32))
                elif args.float_edges:
                    # Alternate finite extremes (accepted layouts) with full
                    # exceptional values (numeric fallback) across TOP cycles.
                    pool=edges if (k//306)&1 else [v for v in edges if v&0x7f800000 != 0x7f800000]
                    word(address+4*i,pool[(k//18+3*i+shift)%len(pool)])
                else: fl(address+4*i,math.sin((k//18+i+shift)*.25)*3)
        if (k//18)&1: word(matrix,0x3f800000)
        field('fsp',((k//18)&7) if variant!=15 else 8+(k&7))
        reg(0,output);reg(1,matrix);reg(2,source)
    elif name == 'f_000B5B40':
        left, right = 0x18000 + k % 4, 0x1a000 + k % 4
        if args.matrix_boundaries:
            left, right = 0x18000, 0x1a000
        output = [0x22000, left, right, left + 4, left + 0x20000][k % 5]
        if args.bounded_matrices or args.matrix_boundaries:
            alignment = 0 if args.matrix_boundaries else k % 4
            variant = (k // 520) % 12 if args.matrix_boundaries else (k // 4) % 12
            output = 0x22000 + alignment
            if variant == 1: output = left
            elif variant == 2: output = right
            elif variant == 3: right = output = left
            elif variant == 4: right = left
            elif variant == 5: right, output = left + 0x20000, left
            elif variant == 6: output = left + 4
            elif variant == 7: output = left + 0x20004
            elif variant == 8:
                sp = 0x62008
                reg(4, sp)
                word(sp, 0x12345678)
            elif variant == 9: left = 0x18ff0 + alignment
            elif variant == 10: right = 0x1aff8 + alignment
            elif variant == 11: output = 0x22ffc + alignment
        for address, angle in [(left, k * .125), (right, k * -.25)]:
            values = [1, math.cos(angle), math.sin(angle), 0,
                      -math.sin(angle), math.cos(angle), 0, 0, 0, 1,
                      k * .25, -k * .125, 3]
            for i, value in enumerate(values):
                if args.bounded_matrices:
                    value = ((rng.randrange(2) << 31) | (rng.randrange(98,156) << 23) |
                             rng.getrandbits(23))
                    if rng.randrange(8) == 0: value &= 0x80000000
                    word(address + i * 4, value)
                elif args.random_floats: word(address + i * 4, rng.getrandbits(32))
                elif args.float_edges:
                    edge_index = ((k // 4) // len(edges) + 5 * i + 2) if address == right else (k // 4 + 3 * i)
                    word(address + i * 4, edges[edge_index % len(edges)])
                else: fl(address + i * 4, value)
        if args.matrix_boundaries:
            # Adjacent representable values on both sides of each bound,
            # exceptional values, and signed zeros. Cycle both operands and
            # every component; the expanded layouts also exercise aliases.
            boundaries = [0, 0x80000000, 0x307fffff, 0x30800000,
                          0x30800001, 0x4e7fffff, 0x4e800000, 0x4e800001,
                          0xb07fffff, 0xb0800000, 0xb0800001, 0xce7fffff,
                          0xce800000, 0xce800001, 1, 0x80000001,
                          0x7f800000, 0xff800000, 0x7fc01234, 0x7f801234]
            case = k % 520
            operand = (left, right)[(case // 13) % 2]
            word(operand + 4 * (case % 13),
                 boundaries[(case // 26) % len(boundaries)])
        word(sp + 4, left)
        word(sp + 8, right)
        word(sp + 12, output)
    else:
        raise AssertionError(name)
    return bytes(memory), bytes(context)


baseline, candidate = Machine(args.baseline), Machine(args.candidate)
report = {}
for name in args.functions:
    rows = []
    # Warm up lazy environment lookups equally before measuring.
    memory, context = fixture(name, 0)
    baseline.call(name, memory, context)
    candidate.call(name, memory, context)
    if args.native_matrix:
        candidate.matrix_mode(args.native_matrix)
    for fpscr in args.fpscr:
        for k in range(args.cases):
            memory, context = fixture(name, k)
            old, new = baseline.call(name, memory, context, fpscr), candidate.call(name, memory, context, fpscr)
            for index, field_name in [(0, 'context'), (1, 'guest-memory')]:
                if old[index] != new[index]:
                    prefix = out / f'mismatch-{name}-{k}-{fpscr:08x}-{field_name}'
                    prefix.with_suffix('.baseline').write_bytes(old[index])
                    prefix.with_suffix('.candidate').write_bytes(new[index])
                    offsets = [i for i, (a, b) in enumerate(zip(old[index], new[index])) if a != b]
                    prefix.with_suffix('.json').write_text(json.dumps({'case': k, 'fpscr': fpscr,
                        'different_bytes': len(offsets), 'first_offsets': offsets[:32],
                        'native_math_counts': old[4]}, indent=2) + '\n')
                    raise AssertionError((name, k, hex(fpscr), field_name, str(prefix)))
            assert old[4] == new[4], (name, k, hex(fpscr), 'native math fast/fallback paths')
            assert old[5] == new[5], (name, k, hex(fpscr), 'native FP status', hex(old[5]), hex(new[5]))
            rows.append({'case': k, 'fpscr': fpscr, 'baseline': old[2], 'candidate': new[2],
                         'firmware_copies': {'baseline': old[3], 'candidate': new[3]},
                         'native_math_counts': old[4], 'point_math_counts': new[6],
                         'matrix_neon_counts': new[7]})
    before, after = (sum(r[n] for r in rows) for n in ('baseline', 'candidate'))
    report[name] = {'cases': len(rows), 'baseline_instructions': before,
                    'candidate_instructions': after, 'change_percent': (after / before - 1) * 100,
                    'faster_cases': sum(r['candidate'] < r['baseline'] for r in rows),
                    'slower_cases': sum(r['candidate'] > r['baseline'] for r in rows),
                    'native_math_counts': dict(zip(['matrix_fast', 'quaternion_fast',
                        'matrix_fallback', 'quaternion_fallback'],
                        [sum(r['native_math_counts'][i] for r in rows) for i in range(4)]))}
    (out / (name + '.json')).write_text(json.dumps(rows, indent=2) + '\n')
report = {'unicorn': unicorn.__version__, 'results': report, 'units': 'instructions, not cycles or FPS',
          'native_matrix': args.native_matrix,
          'float_edges': args.float_edges, 'random_floats': args.random_floats, 'fpscr': args.fpscr,
          'bounded_matrices': args.bounded_matrices,
          'matrix_boundaries': args.matrix_boundaries,
          'guest_trace_enabled': {'baseline': baseline.guest_trace_enabled,
                                  'candidate': candidate.guest_trace_enabled},
          'inputs': {n: {'path': str(p.resolve()), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                     for n, p in [('baseline', args.baseline), ('candidate', args.candidate)]}}
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
