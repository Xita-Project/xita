#!/usr/bin/env python3
"""Carry FP state through repeated retained UV callers and real publication.

Requires caller.elf from test_model_uv_caller.py. Only next-call GPR/argument
setup is synthetic: no intervening full material/model walk is executed.
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
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_FPSCR


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    elf = args.elf.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = json.loads((elf.parent / 'caller-results.json').read_text())
    for relative in ('recomp/kernel/xk_model_uv.c', 'recomp/kernel/xk_owner_phase.c',
                     'tools/tests/model_uv_caller_arm.c'):
        path = S / relative
        assert receipt['hashes'][str(path)] == hashlib.sha256(path.read_bytes()).hexdigest(), \
            'Rebuild the caller fixture after changing ' + relative
    m = RuntimeMachine(elf, True)

    def read(address, size): return bytes(m.uc.mem_read(address, size))
    def write(address, value): m.uc.mem_write(address, value)
    def word(address): return struct.unpack('<I', read(address, 4))[0]
    def guest(address):
        return RAM + word(word(m.symbols['g_xpt']) + (address >> 12) * 4) + (address & 4095)
    def counts():
        values = struct.unpack('<22I', read(m.symbols['counts'], 88))
        return values[3] + values[4], values[5]

    publication_size = word(m.symbols['publication_size'])
    def snapshot():
        return (read(m.context, m.layout['size']), read(RAM, SIZE),
                read(m.symbols['xd3d_state'], publication_size))
    def restore(state):
        write(m.context, state[0]); write(RAM, state[1])
        write(m.symbols['xd3d_state'], state[2])

    frontiers = []
    def frontier(uc, address, size, user):
        if address == m.symbols['uv_frontier'] & ~1:
            frontiers.append((snapshot(), uc.reg_read(UC_ARM_REG_FPSCR)))
    m.uc.hook_add(UC_HOOK_CODE, frontier)
    rows = []
    modes = [0, 0x10, 0x90, 0x03000000, 0x03000090, 0x00c00000]
    sequences = {'front': ['front'] * 6, 'back': ['back'] * 6,
                 'alternating': ['front', 'back'] * 3}
    for top in range(8):
        for initial_fp in modes:
            for name, routes in sequences.items():
                # Prepare both caller entry register sets. Neither setup is run
                # between calls: all FP slots, status and controls carry forward.
                m.call('arm_caller_prepare', (1, top, 0))
                back_gprs = read(m.context, 32)
                m.call('arm_caller_prepare', (0, top, 0)); m.call('arm_reset')
                front_gprs = read(m.context, 32)
                fp = initial_fp
                for step, route in enumerate(routes):
                    write(m.context, front_gprs if route == 'front' else back_gprs)
                    if route == 'front':
                        write(guest(0x907e8), struct.pack('<6I', 0, 0, 0, 0, 0, 0x418c0000))
                    state = snapshot(); before = counts()
                    frontiers.clear()
                    original = m.call('arm_' + route + '_original', fpscr=fp)
                    reference = snapshot(); reference_frontiers = list(frontiers)
                    restore(state); frontiers.clear()
                    candidate = m.call('arm_' + route + '_candidate', fpscr=fp)
                    after = counts()
                    assert reference == snapshot(), (name, top, initial_fp, step, 'state')
                    assert reference_frontiers == frontiers, (name, top, initial_fp, step, 'publication entry')
                    assert original['fpscr'] == candidate['fpscr'], (name, top, initial_fp, step, 'FPSCR')
                    hit, cold = after[0] - before[0], after[1] - before[1]
                    assert hit + cold == 1, (name, top, initial_fp, step, 'admission')
                    rows.append(dict(sequence=name, route=route, top=top, initial_fp=initial_fp,
                                     step=step, fpscr_in=fp, fpscr_out=candidate['fpscr'],
                                     hit=hit, cold=cold, original_instructions=original['instructions'],
                                     candidate_instructions=candidate['instructions']))
                    fp = candidate['fpscr']
    result = dict(comparisons=len(rows), rows=rows,
                  scope='Synthetic repeated caller entries carrying previous FP state; actual retained UV caller, original arithmetic and production shader-constant publication. No intervening full model code, scheduler or GPU.',
                  hashes={str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                          for path in [elf, Path(__file__), elf.parent / 'caller.c']})
    (output / 'sequence-results.json').write_text(json.dumps(result, indent=2) + '\n')
    for name in sequences:
        for initial_fp in modes:
            selected = [row for row in rows if row['sequence'] == name and row['top'] == 0 and row['initial_fp'] == initial_fp]
            print(name, hex(initial_fp), 'hits', ''.join(str(row['hit']) for row in selected),
                  'instructions', sum(row['original_instructions'] for row in selected),
                  sum(row['candidate_instructions'] for row in selected))
    print('PASS', len(rows))


if __name__ == '__main__':
    main()
