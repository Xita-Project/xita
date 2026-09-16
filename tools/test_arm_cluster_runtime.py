#!/usr/bin/env python3
"""Instruction/correctness comparison of the complete guarded cluster adapter.

Snapshot construction, replay, validation, publication and original allocation/
list tail execute. Vita libc forwards bulk copies/clears to firmware: those bytes
are counted separately. The native scheduler, uncontended kernel imports and
real allocator bookkeeping are not modeled.
This is neither Vita3K validation nor a Vita frame-time/FPS measurement.
"""
from pathlib import Path
import argparse
import bisect
from collections import Counter
import json
import os
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.test_cluster_runtime import generate_worker_reference
import test_arm_model_palette as base
base.SIZE = 8 << 20
from test_arm_model_palette import Machine, RAM, PT, STACK, END, SIZE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR)


class RuntimeMachine(Machine):
    def __init__(self, path, profile=False):
        super().__init__(path)
        # Execute the libc wrappers; model only their firmware targets.
        self.imports = {a: n for a, n in self.imports.items() if n not in ('memcpy', 'memmove', 'memset', 'getenv')}
        for symbol, op in (('sceClibMemcpy', 'memcpy'), ('sceClibMemset', 'memset'), ('sceClibMemmove', 'memmove')):
            if symbol in self.symbols:
                self.imports[self.symbols[symbol] & ~1] = op
        self.kernel = {a & ~1: n for n, a in self.symbols.items() if n.startswith('sceKernel')}
        self.profile = profile
        with path.open('rb') as f:
            elf = ELFFile(f)
            self.functions = sorted((s['st_value'] & ~1, s['st_size'], s.name) for s in
                elf.get_section_by_name('.symtab').iter_symbols()
                if s['st_info']['type'] == 'STT_FUNC' and s['st_size'])
        self.starts = [s[0] for s in self.functions]
        self.context = struct.unpack('<I', self.uc.mem_read(self.symbols['arm_context_ptr'], 4))[0]
        self.uc.mem_write(self.symbols['g_img_base'], struct.pack('<I', RAM + (4 << 20)))
        self.uc.mem_write(PT, struct.pack('<1024I', *[(i ^ 1) * 4096 for i in range(1024)]))

    def step(self, uc, address, size, user):
        if self.profile:
            i = bisect.bisect_right(self.starts, address) - 1
            start, length, name = self.functions[i] if i >= 0 else (0, 0, 'unknown')
            self.by_function[name if start <= address < start + length else 'unknown'] += 1
            self.by_address[address] += 1
        name = self.kernel.get(address)
        if name:
            self.kernel_calls[name] = self.kernel_calls.get(name, 0) + 1
        super().step(uc, address, size, user)

    def call(self, name, args=(), fpscr=0):
        u = self.uc
        for reg, value in zip((UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2), args):
            u.reg_write(reg, value)
        u.reg_write(UC_ARM_REG_SP, STACK + 65024)
        u.reg_write(UC_ARM_REG_LR, END | 1)
        u.reg_write(UC_ARM_REG_FPSCR, fpscr)
        self.instructions = self.copies = self.copy_bytes = self.yields = 0
        self.kernel_calls = {}
        self.by_function = Counter(); self.by_address = Counter()
        u.emu_start(self.symbols[name] | 1, END, count=20000000)
        assert u.reg_read(UC_ARM_REG_PC) == END, (name, hex(u.reg_read(UC_ARM_REG_PC)))
        result = dict(instructions=self.instructions, kernel_imports=self.kernel_calls,
                    firmware_copy_calls=self.copies, firmware_copy_bytes=self.copy_bytes,
                    fpscr=u.reg_read(UC_ARM_REG_FPSCR))
        if self.profile:
            assert sum(self.by_function.values()) == self.instructions
            result.update(by_function=dict(self.by_function),
                          hot_addresses={hex(a): n for a, n in self.by_address.most_common(40)})
        return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--quick', action='store_true')
    p.add_argument('--profile', action='store_true')
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
    generate_worker_reference(a.xbe, a.manifest, a.out)
    cc = os.environ.get('ARM_CC', '/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    flags = ['-O2', '-g', '-std=gnu11', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon',
             '-fno-strict-aliasing', '-ffp-contract=off', '-frounding-math',
             '-ffunction-sections', '-fdata-sections', '-DXV_EXPERIMENTAL_OBJECT_JOBS',
             '-DXV_WORKER_QUERY', '-DXV_TYPED_CLUSTER_QUERY', '-DXV_WORKER_QUERY_TEST',
             '-I' + str(ROOT / 'recomp'), '-I' + str(a.out)]
    sources = [a.out / 'worker-reference.c', ROOT / 'tools/tests/cluster_runtime_arm.c',
               ROOT / 'tools/tests/cluster_runtime_arm_imports.c', ROOT / 'recomp/xv_x86rt.c']
    sources += [ROOT / 'recomp/kernel' / (n + '.c') for n in
                ('xk_cluster_runtime', 'xk_cluster_snapshot', 'xk_cluster_query', 'xk_cluster_query_replay')]
    commands, objects = [], []
    for i, source in enumerate(sources):
        obj = a.out / f'unit-{i}.o'
        command = [cc, *flags, '-c', str(source), '-o', str(obj)]
        subprocess.run(command, check=True); commands.append(command); objects.append(str(obj))
    elf = a.out / 'runtime.elf'
    names = ('arm_prepare', 'arm_original', 'arm_candidate', 'arm_snapshot', 'arm_finish',
             'layout', 'arm_context_ptr', 'arm_admitted', 'arm_allocations', 'arm_allocated_bytes')
    command = [cc, *flags, *objects, '-nostdlib',
               '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,' + ','.join('--undefined=' + n for n in names),
               '-lm', '-lc', '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True); commands.append(command)
    (a.out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    m = RuntimeMachine(elf, a.profile); rows = []
    # The live visited table crosses a page after entry 20. Include both sides
    # and Blood Gulch's 30-cluster size, not just large synthetic traversals.
    specs = [(n, 1024, 0) for n in (1, 7, 20, 21, 30, 31, 65, 256)] + [(7, 1024, t) for t in range(1, 7)]
    if not a.quick:
        specs += [(n, capacity, 0) for n in (7, 65, 256) for capacity in (0, 1, 8)]
    for n, capacity, tweak in specs:
        m.call('arm_finish')
        m.uc.mem_write(RAM, bytes(SIZE))
        m.call('arm_prepare', (n, capacity, tweak))
        before = bytes(m.uc.mem_read(RAM, SIZE))
        context = bytes(m.uc.mem_read(m.context, m.layout['size']))
        for rounding in range(1 if a.quick else 4):
            for control in ((0,) if a.quick else (0, 0x10, 0x01000000, 0x02000000, 0x0300009f)):
                fpscr = (rounding << 22) | control
                m.call('arm_finish')
                m.uc.mem_write(RAM, before); m.uc.mem_write(m.context, context)
                original = m.call('arm_original', fpscr=fpscr)
                expected_context = bytes(m.uc.mem_read(m.context, m.layout['size']))
                expected_memory = bytes(m.uc.mem_read(RAM, SIZE))
                m.uc.mem_write(RAM, before); m.uc.mem_write(m.context, context)
                m.uc.mem_write(m.symbols['arm_admitted'], bytes(4))
                snapshot = m.call('arm_snapshot', fpscr=fpscr)
                assert bytes(m.uc.mem_read(RAM, SIZE)) == before and snapshot['fpscr'] == fpscr
                candidate = m.call('arm_candidate', fpscr=fpscr)
                row = dict(n=n, capacity=capacity, tweak=tweak, rounding=rounding, control=control,
                           original=original, snapshot=snapshot, candidate=candidate,
                           admitted=struct.unpack('<I', m.uc.mem_read(m.symbols['arm_admitted'], 4))[0],
                           allocations=struct.unpack('<I', m.uc.mem_read(m.symbols['arm_allocations'], 4))[0],
                           allocated_bytes=struct.unpack('<I', m.uc.mem_read(m.symbols['arm_allocated_bytes'], 4))[0])
                checks = dict(context=bytes(m.uc.mem_read(m.context, m.layout['size'])) == expected_context,
                              memory=bytes(m.uc.mem_read(RAM, SIZE)) == expected_memory,
                              fpscr=candidate['fpscr'] == original['fpscr'],
                              admission=row['admitted'] == (0 if n == 1 or tweak in (1, 2, 3, 5) else 1))
                if not all(checks.values()):
                    (a.out / 'failure.json').write_text(json.dumps(dict(row, checks=checks), indent=2))
                    raise AssertionError((row, checks))
                rows.append(row)
        (a.out / 'result.json').write_text(json.dumps(rows, indent=2) + '\n')
        sample = rows[-1]
        print('PASS full ARM runtime', n, capacity, tweak, 'original / typed / snapshot instructions',
              sample['original']['instructions'], sample['candidate']['instructions'], sample['snapshot']['instructions'], flush=True)
    print('PASS', len(rows), 'full context/memory/FPSCR comparisons; kernel calls and real allocation overhead excluded')


if __name__ == '__main__':
    main()
