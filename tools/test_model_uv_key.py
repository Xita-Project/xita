#!/usr/bin/env python3
"""Check UV cache condition-bit keys against an owned production ARM fixture.

First run test_model_uv.py to create uv.elf. This runner compares complete
contexts, guest arenas and raw FPSCR against its retained original function.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

S = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(S / 'tools'), str(S)]
from test_arm_cluster_runtime import RuntimeMachine, RAM, SIZE


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    elf = args.elf.resolve()
    E = elf.parent
    O = args.output_dir.resolve()
    O.mkdir(parents=True, exist_ok=True)
    m = RuntimeMachine(elf, True)
    rows = []
    def rd(a, n): return bytes(m.uc.mem_read(a, n))
    def wr(a, b): m.uc.mem_write(a, b)
    def word(a): return struct.unpack('<I', rd(a, 4))[0]
    def count(name):
        values = struct.unpack('<22I', rd(m.symbols['counts'], 88))
        return {'hits': values[3]+values[4], 'misses': values[5], 'declines': sum(values[13:19])}[name]
    def snap(): return rd(m.context, m.layout['size']), rd(RAM, SIZE)
    def restore(s): wr(m.context, s[0]); wr(RAM, s[1])
    def altered(s, fsw=None, fcw=None, top=None):
        c = bytearray(s[0])
        if fsw is not None: struct.pack_into('<H', c, m.layout['fsw'], fsw)
        if fcw is not None: struct.pack_into('<H', c, m.layout['fsw'] + 2, fcw)
        if top is not None: struct.pack_into('<I', c, m.layout['fsp'], top)
        return bytes(c), s[1]
    def compare(name, state, fp, expected):
        restore(state)
        old = m.call('arm_original', fpscr=fp)
        reference = snap()
        restore(state)
        before = count(expected)
        new = m.call('arm_candidate', fpscr=fp)
        same = reference == snap() and old['fpscr'] == new['fpscr']
        observed = count(expected) - before
        row = dict(name=name, expected=expected, observed=observed,
                   state_equal=same, fpscr=old['fpscr'],
                   original_instructions=old['instructions'],
                   candidate_instructions=new['instructions'])
        rows.append(row)
        if not same or observed != 1:
            (O / 'production-dead-bits-failure.json').write_text(json.dumps(row, indent=2))
            raise AssertionError(row)
    masks = [sum(bit for n, bit in enumerate([0x100, 0x200, 0x400, 0x4000])
                 if selector & (1 << n)) for selector in range(16)]
    # Full independent Cartesian product of guest and native condition bits,
    # across every x87 TOP. Each candidate hit follows one original cold call.
    for top in range(8):
        m.call('arm_prepare', (top, 0, 0)); m.call('arm_reset')
        state = snap(); fsw = struct.unpack_from('<H', state[0], m.layout['fsw'])[0]
        compare(f'top{top}-cold', state, 0x03000090, 'misses')
        for guest_bits in masks:
            current = altered(state, fsw=(fsw & ~0x4700) | guest_bits)
            for native_bits in range(16):
                compare(f'top{top}-{guest_bits:04x}-{native_bits:x}', current,
                        0x03000090 | (native_bits << 28), 'hits')
        print('PASS condition product TOP', top, flush=True)
    # Exercise the mask omission with all rounding and FZ/DN combinations;
    # keep live controls and cumulative flags identical within each cache group.
    for rc in range(4):
        for mode in range(4):
            m.call('arm_prepare', (3, 0, 1)); m.call('arm_reset')
            state = snap(); fsw = struct.unpack_from('<H', state[0], m.layout['fsw'])[0]
            fp = (rc << 22) | (mode << 24) | 0x90
            compare(f'control-{rc}-{mode}-cold', state, fp, 'misses')
            for index, guest_bits in enumerate(masks):
                compare(f'control-{rc}-{mode}-{index}',
                        altered(state, fsw=(fsw & ~0x4700) | guest_bits),
                        fp | ((15 - index) << 28), 'hits')
    # Live inputs must still miss. In particular, never erase/replay incoming
    # sticky exceptions merely because the condition nibble is dead.
    for bit in [0, 1, 2, 3, 4, 5, 6, 7, 11, 12, 13, 15]:
        m.call('arm_prepare', (0, 0, 0)); m.call('arm_reset')
        state = snap(); fsw = struct.unpack_from('<H', state[0], m.layout['fsw'])[0]
        compare(f'fsw-live{bit}-cold', state, 0x03000090, 'misses')
        compare(f'fsw-live{bit}-miss', altered(state, fsw=fsw ^ (1 << bit)),
                0x03000090, 'misses')
    for bit in [0, 1, 2, 3, 4, 7, 22, 23, 24, 25, 27]:
        m.call('arm_prepare', (0, 0, 0)); m.call('arm_reset'); state = snap()
        compare(f'fpscr-live{bit}-cold', state, 0x03000090, 'misses')
        compare(f'fpscr-live{bit}-miss', state, 0x03000090 ^ (1 << bit), 'declines' if bit == 27 else 'misses')
    result = dict(comparisons=len(rows), rows=rows,
                  scope='Actual production helper and real owner predicate; original UV/waveform0/Vita libm, complete context, arena and raw FPSCR. Synthetic native thread/fiber identity; no hardware hit-rate claim.',
                  hashes={str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in [S/'recomp/kernel/xk_model_uv.c', elf, E/'original.c', S/'tools/tests/model_uv_arm.c']})
    (O/'production-dead-bits-results.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS', len(rows), flush=True)


if __name__ == '__main__':
    main()
