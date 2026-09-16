#!/usr/bin/env python3
"""VitaSDK instruction-level numerical oracle for the direct query prototype.

Snapshot construction and publication are outside this test. Instruction counts
exclude modeled libc copies/clears and are not Vita cycles or FPS estimates.
"""
from pathlib import Path
import argparse
import json
import os
import struct
import subprocess
import test_arm_model_palette as base
base.SIZE = 8 << 20
from test_arm_model_palette import Machine, RAM, PT, STACK, END, SIZE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR)

ROOT = Path(__file__).resolve().parents[1]
SP = 0x2b0000


class QueryMachine(Machine):
    def __init__(self, path):
        super().__init__(path)
        self.uc.mem_write(self.symbols['g_img_base'], struct.pack('<I', RAM + (4 << 20)))
        self.pages = [(i ^ 1) * 4096 for i in range(1024)]
        self.uc.mem_write(PT, struct.pack('<1024I', *self.pages))
        self.result_layout = struct.unpack('<5I', self.uc.mem_read(self.symbols['result_layout'], 20))

    def call(self, name, args=(), fpscr=0):
        u = self.uc
        for register, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2), args):
            u.reg_write(register, value)
        u.reg_write(UC_ARM_REG_SP, STACK + 65024)
        u.reg_write(UC_ARM_REG_LR, END | 1)
        u.reg_write(UC_ARM_REG_FPSCR, fpscr)
        self.instructions = self.copies = self.copy_bytes = self.yields = 0
        u.emu_start(self.symbols[name] | 1, END, count=20000000)
        assert u.reg_read(UC_ARM_REG_PC) == END, (name, hex(u.reg_read(UC_ARM_REG_PC)))
        return dict(instructions=self.instructions, imported_copy_bytes=self.copy_bytes,
                    yields=self.yields)

    def guest(self, address, size):
        parts = []
        while size:
            n = min(size, 4096 - (address & 4095))
            parts.append(bytes(self.uc.mem_read(RAM + self.pages[address >> 12] +
                                               (address & 4095), n)))
            address += n; size -= n
        return b''.join(parts)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--quick', action='store_true')
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get('ARM_CC', '/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    flags = ['-O2', '-g', '-std=gnu11', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon',
             '-fno-strict-aliasing', '-ffp-contract=off', '-frounding-math',
             '-ffunction-sections', '-fdata-sections', '-DCLUSTER_ARM',
             '-I' + str(ROOT / 'recomp'), '-I' + str(a.reference.parent)]
    commands, objects = [], []
    for i, source in enumerate((a.reference, ROOT / 'tools/tests/cluster_query.c',
            ROOT / 'tools/tests/cluster_query_arm_stubs.c',
            ROOT / 'recomp/kernel/xk_cluster_query.c', ROOT / 'recomp/xv_x86rt.c')):
        obj = a.out / f'unit-{i}.o'
        command = [cc, *flags, '-c', str(source), '-o', str(obj)]
        subprocess.run(command, check=True)
        commands.append(command); objects.append(str(obj))
    elf = a.out / 'direct-query.elf'
    command = [cc, *flags, *objects, '-nostdlib',
        '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,'
        '--undefined=arm_prepare,--undefined=arm_tweak,--undefined=arm_original,'
        '--undefined=arm_candidate,--undefined=layout,--undefined=result_layout',
        '-lm', '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True); commands.append(command)
    (a.out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    m = QueryMachine(elf)
    rows = []
    # Same immutable inputs are reused for every native FP mode. Preparation is
    # unmeasured and always uses the default control state.
    specs = [(7, 0, k) for k in range(12)]
    if not a.quick:
        specs += [(31, style, 0) for style in range(8)]
        specs += [(n, 0, 0) for n in (65, 256)]
    for n, style, tweak in specs:
        m.uc.mem_write(RAM, bytes(SIZE))
        m.call('arm_prepare', (7, n, style))
        m.call('arm_tweak', (tweak,))
        before = bytes(m.uc.mem_read(RAM, SIZE))
        context = bytes(m.uc.mem_read(m.symbols['arm_context'], m.layout['size']))
        budget = struct.unpack_from('<I', context, m.layout['preempt'])[0]
        visited = m.guest(0x2d2fb0, 1024)
        assert visited == bytes(m.uc.mem_read(m.symbols['visited'], 1024))
        for rounding in range(1 if a.quick else 4):
            for control in ((0,) if a.quick else (0, 0x10, 0x01000000, 0x02000000, 0x0300009f)):
                fpscr = (rounding << 22) | control
                m.uc.mem_write(RAM, before)
                m.uc.mem_write(m.symbols['arm_context'], context)
                original = m.call('arm_original', fpscr=fpscr)
                ctx = bytes(m.uc.mem_read(m.symbols['arm_context'], m.layout['size']))
                count = struct.unpack_from('<H', ctx, m.layout['r'])[0]
                expected_order = m.guest(SP - 128, min(count, 64) * 2)
                expected_epoch = struct.unpack('<I', m.uc.mem_read(RAM + (4 << 20) + 0x2d2fac, 4))[0]
                expected_visited = m.guest(0x2d2fb0, 1024)
                backedges = budget - struct.unpack_from('<I', ctx, m.layout['preempt'])[0]
                m.uc.mem_write(RAM, before)
                candidate = m.call('arm_candidate', fpscr=fpscr)
                admitted = struct.unpack('<I', m.uc.mem_read(m.symbols['arm_admitted'], 4))[0]
                record = dict(n=n, style=style, tweak=tweak, rounding=rounding,
                              control=control, admitted=admitted,
                              original=original, candidate=candidate)
                if tweak in (0, 1, 2, 9, 11):
                    assert admitted, ('ordinary-input coverage', record)
                if admitted:
                    size, co, ep, ch, be = m.result_layout
                    result = bytes(m.uc.mem_read(m.symbols['arm_result'], size))
                    actual_count = struct.unpack_from('<I', result, co)[0]
                    actual_epoch = struct.unpack_from('<I', result, ep)[0]
                    actual_edges = struct.unpack_from('<I', result, be)[0]
                    actual_visited = bytearray(visited)
                    for i in range(256):
                        word = struct.unpack_from('<I', result, ch + (i >> 5)*4)[0]
                        if word & (1 << (i & 31)):
                            struct.pack_into('<I', actual_visited, i*4, actual_epoch)
                    checks = dict(count=actual_count == count,
                        order=result[:min(count, 64)*2] == expected_order,
                        epoch=actual_epoch == expected_epoch,
                        visited=actual_visited == expected_visited,
                        budget=actual_edges == backedges and original['yields'] == 0)
                    if not all(checks.values()):
                        (a.out / 'failure.json').write_text(json.dumps(dict(record,checks=checks),indent=2))
                        raise AssertionError((record, checks, count, actual_count, backedges, actual_edges))
                # Candidate must not publish any byte into the guest arena.
                assert bytes(m.uc.mem_read(RAM, SIZE)) == before
                rows.append(record)
        print('PASS ARM numerical', n, style, tweak, flush=True)
        (a.out / 'result.json').write_text(json.dumps(rows, indent=2) + '\n')
    accepted = sum(r['admitted'] for r in rows)
    assert accepted > len(rows) // 2
    print('PASS', accepted, 'accepted /', len(rows), 'ARM numerical cases; no live publication proof')


if __name__ == '__main__':
    main()
