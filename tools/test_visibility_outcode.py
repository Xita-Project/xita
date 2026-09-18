#!/usr/bin/env python3
"""Compare local outcode scratch with an exact retained Vita ARM object.

Unicorn checks semantics, not Vita performance. Inputs and lifted bodies stay
in private output. Unused unresolved object symbols fail if execution reaches
them; only libc copy services are modeled.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import test_arm_model_palette as base
base.SIZE = 4 << 20
from test_arm_model_palette import RAM, PT, CTX, STACK, END, SIZE, EDGES
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR
from visibility_outcode import transform, FLAG, PIN


def sha(data):
    return hashlib.sha256(data).hexdigest()


def build(stage, out, cc):
    source = (stage / 'recomp/code_000.c').read_text()
    matches = re.findall(r'^void f_00012420\([^\n]*\)\n\{\n.*?^\}\n', source, re.M | re.S)
    if len(matches) != 1 or sha(matches[0].encode()) != PIN:
        raise ValueError('retained leaf identity mismatch')
    body = matches[0]
    (out / 'candidate.c').write_text('#include "xv_x86rt.h"\n' +
                                  transform(body).replace('f_00012420', 'vo_candidate'))
    flags = ['-O2', '-fno-strict-aliasing', '-ffp-contract=off', '-mthumb',
             '-mcpu=cortex-a9', '-mfpu=neon', '-std=gnu11', '-I' + str(stage / 'recomp')]
    commands = []
    objects = [stage / 'build/recomp/code_000.o', stage / 'build/recomp/xv_x86rt.o']
    def run(args):
        commands.append(args)
        subprocess.run(args, check=True)
    run([cc, *flags, '-c', str(ROOT / 'tools/tests/visibility_portal_arm.c'), '-o', str(out / 'fixture.o')])
    for enabled in (0, 1):
        obj = out / ('candidate-' + str(enabled) + '.o')
        run([cc, *flags, '-D' + FLAG + '=' + str(enabled), '-fstack-usage', '-c',
             str(out / 'candidate.c'), '-o', str(obj)])
        run([cc, *flags, str(out / 'fixture.o'), str(obj), *map(str, objects), '-nostdlib',
             '-Wl,-Ttext=0x10000,-e,test_boot,--unresolved-symbols=ignore-all,--wrap=xv_preempt',
             '-lm', '-lgcc', '-o', str(out / ('candidate-' + str(enabled) + '.elf'))])
    return dict(commands=commands, retained_objects={str(p): sha(p.read_bytes()) for p in objects}, body_sha256=PIN)


class Machine(base.Machine):
    def run(self, name, fixture):
        memory, context, pages, fpscr = fixture
        u = self.uc
        u.mem_write(RAM, memory); u.mem_write(CTX, context)
        u.mem_write(PT, struct.pack('<' + 'I' * len(pages), *pages))
        u.mem_write(STACK, bytes(65536))
        u.reg_write(UC_ARM_REG_R0, CTX); u.reg_write(UC_ARM_REG_SP, STACK + 65024)
        u.reg_write(UC_ARM_REG_LR, END | 1); u.reg_write(UC_ARM_REG_FPSCR, fpscr)
        self.instructions = self.copies = self.copy_bytes = self.yields = 0
        u.emu_start(self.symbols[name] | 1, END, count=20000)
        if u.reg_read(UC_ARM_REG_PC) != END:
            raise AssertionError('leaf did not return')
        return dict(context=bytes(u.mem_read(CTX, self.layout['size'])).hex(),
                    memory=sha(bytes(u.mem_read(RAM, SIZE))),
                    pages=sha(bytes(u.mem_read(PT, 4 << 20))),
                    fpscr=u.reg_read(UC_ARM_REG_FPSCR), instructions=self.instructions,
                    copies=self.copies, copy_bytes=self.copy_bytes, yields=self.yields)


def fixture(layout, seed, variant, fpscr, values=None):
    rng = random.Random(seed)
    memory = bytearray(SIZE)
    context = bytearray(rng.randbytes(layout['size']))
    pages = [(i ^ 0x40) * 4096 for i in range(SIZE // 4096)]
    plane, point, sp = 0x10000, 0x24000, 0x30800
    if variant == 'plane-page': plane = 0x10f81
    if variant == 'point-page': point = 0x24ffe
    if variant == 'unaligned': plane += 1; point += 3
    if variant == 'push-first-y': sp = plane + 0x80
    if variant == 'push-last-d': sp = plane + 0xb8
    if variant == 'push-point': sp = point + 8
    if variant == 'shared-input': point = plane + 0xa8
    if variant == 'mapped-stack-alias':
        pages[sp >> 12] = pages[plane >> 12]; sp = (sp & ~4095) + 0x80
    def write(address, data):
        for i, v in enumerate(data):
            a = (address + i) & 0xffffffff
            memory[pages[a >> 12] + (a & 4095)] = v
    def word(a, v): write(a, struct.pack('<I', v))
    if values is None:
        values = [struct.unpack('<I', struct.pack('<f', rng.uniform(-100, 100)))[0] for _ in range(20)]
    for i in range(16): word(plane + 0x78 + 4*i, values[i])
    for i in range(3): word(point + 4*i, values[16+i])
    word(0x1f0a68, values[19])
    for n, v in [(1, plane), (2, point), (4, sp)]:
        struct.pack_into('<I', context, 4*n, v)
    struct.pack_into('<I', context, layout['fsp'], seed % 8 if variant != 'high-fsp' else 0xffffffff)
    return bytes(memory), bytes(context), pages, fpscr


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    p.add_argument('--cc', default='/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    args = p.parse_args()
    if not __debug__: raise RuntimeError('identity checks require Python without -O')
    if args.output_dir.resolve().is_relative_to(ROOT): raise ValueError('output must stay private')
    args.output_dir.mkdir(parents=True, exist_ok=False)
    receipt = build(args.stage, args.output_dir, args.cc)
    machines = [Machine(args.output_dir / ('candidate-' + str(v) + '.elf')) for v in (0, 1)]
    cases = []
    variants = ['normal', 'plane-page', 'point-page', 'unaligned', 'push-first-y',
                'push-last-d', 'push-point', 'shared-input', 'mapped-stack-alias', 'high-fsp']
    for i in range(100): cases.append((i, variants[i % len(variants)], (i % 4) << 22, None))
    for mask in range(16):
        # Every combination of the four outcode bits, including strict zero.
        values = [0] * 20
        for plane, bit in enumerate((1, 2, 8, 4)):
            values[plane*4+3] = 0xbf800000 if mask & bit else 0
        cases.append((mask, 'normal', 0, values))
    for i in range(len(EDGES)):
        for mode in range(16):
            fpscr = ((mode & 3) << 22) | ((mode & 4) << 22) | ((mode & 8) << 22) | 0x9f
            values = [EDGES[(i + j*7) % len(EDGES)] for j in range(20)]
            cases.append((i, variants[i % len(variants)], fpscr, values))
    results = []
    for index, case in enumerate(cases):
        state = fixture(machines[0].layout, *case)
        ref = machines[0].run('f_00012420', state)
        if 100 <= index < 116:
            eax = struct.unpack_from('<I', bytes.fromhex(ref['context']))[0]
            if eax != index - 100:
                raise AssertionError('explicit four-plane outcode coverage failed')
        off = machines[0].run('vo_candidate', state)
        new = machines[1].run('vo_candidate', state)
        for name, value in [('off', off), ('candidate', new)]:
            for key in ('context', 'memory', 'pages', 'fpscr', 'yields'):
                if value[key] != ref[key]:
                    failure = dict(index=index, case=case, lane=name, key=key, reference=ref, actual=value)
                    (args.output_dir / 'failure.json').write_text(json.dumps(failure, indent=2)+'\n')
                    raise AssertionError(f'case {index} {name} {key} mismatch; see failure.json')
        results.append(dict(index=index, variant=case[1], reference=ref['instructions'], candidate=new['instructions']))
    receipt.update(result='PASS', cases=len(results), results=results,
                   scope='ARM semantics only; full context, memory, page table, FPSCR; not hardware cycles or FPS')
    (args.output_dir / 'result.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print(json.dumps(dict(result='PASS', cases=len(results), ordinary=results[:10])), flush=True)


if __name__ == '__main__': main()
