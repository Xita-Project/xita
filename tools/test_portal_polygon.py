#!/usr/bin/env python3
"""Compare the experimental typed polygon's live geometry to the owned lift.

This is a geometry/data-interface experiment, not proof of a drop-in xctx ABI.
Outputs are private. Requires VitaSDK, iced-x86, Unicorn and pyelftools.
"""
import argparse
import hashlib
import json
import math
import random
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from games.halo_ce_3925.clip_region import IMAGE_SHA256, SPANS
from recompiler.xita_recomp import Image
from test_native_clip_region import MAPPING
import test_arm_model_palette as base
base.SIZE = 2 << 20
from test_arm_model_palette import RAM, PT, STACK, CTX, END, SIZE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR


def build(stage, xbe, out):
    image = Image(str(xbe))
    assert hashlib.sha256(image.data).hexdigest() == IMAGE_SHA256
    for pc, (size, digest) in SPANS.items():
        assert hashlib.sha256(image.bytes_at(pc, size)).hexdigest() == digest
    # Emit a fresh independent original, never trust a candidate's reference JSON.
    from recompiler import xita_recomp as r
    from recompiler.core.hooks import NoGameHooks
    disc = r.Discovery(image, {}, image.kernel_imports(), lambda *args: None)
    for pc in SPANS:
        disc.add_root(pc); disc.lift_function(disc.functions[pc]); disc.split_blocks(disc.functions[pc])
    emitter = r.Emitter(image, disc, {}, image.kernel_imports(), 'unused', 1, hooks=NoGameHooks())
    source = '#include "xv_x86rt.h"\n' + MAPPING
    source += '\n'.join(f'void f_{pc:08X}(xctx*);' for pc in SPANS) + '\n'
    source += '\n'.join(emitter.emit_function(disc.functions[pc]) for pc in SPANS)
    (out/'original.c').write_text(source)
    cc = str(Path.home()/'vitasdk/bin/arm-vita-eabi-gcc')
    flags = ['-O2', '-std=gnu11', '-fno-strict-aliasing', '-ffp-contract=off',
             '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon', '-ffunction-sections',
             '-fdata-sections', '-fstack-usage', '-I'+str(stage/'recomp'),
             '-I'+str(ROOT/'recomp/kernel')]
    files = [out/'original.c', ROOT/'tools/tests/visibility_portal_arm.c',
             ROOT/'tools/tests/portal_polygon_arm.c', ROOT/'recomp/kernel/xk_portal_polygon_math.c',
             stage/'recomp/xv_x86rt.c']
    commands = []; objects = []
    for i, path in enumerate(files):
        obj = out/f'unit-{i}.o'
        extra = ['-frounding-math'] if path.name == 'xk_portal_polygon_math.c' else []
        cmd = [cc, *flags, *extra, '-c', str(path), '-o', str(obj)]
        subprocess.run(cmd, check=True); commands.append(cmd); objects.append(str(obj))
    elf = out/'test.elf'
    cmd = [cc, *flags, *objects, '-nostdlib',
           '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,'
           '--undefined=f_000B7F10,--undefined=test_candidate,--undefined=layout',
           '-lm', '-lgcc', '-o', str(elf)]
    subprocess.run(cmd, check=True); commands.append(cmd)
    (out/'build.json').write_text(json.dumps(dict(commands=commands,
        image_sha256=IMAGE_SHA256, elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest()), indent=2)+'\n')
    return elf


class Machine(base.Machine):
    def __init__(self, path):
        super().__init__(path)
        self.imports = {a:n for a,n in self.imports.items() if n not in ('getenv','atoi')}

    def run(self, name, fixture, output):
        memory, context, pages, mode = fixture
        u = self.uc
        u.mem_write(RAM, memory); u.mem_write(CTX, context)
        u.mem_write(PT, struct.pack('<'+'I'*len(pages), *pages))
        u.mem_write(STACK, bytes(65536))
        u.reg_write(UC_ARM_REG_R0, CTX); u.reg_write(UC_ARM_REG_SP, STACK+65024)
        u.reg_write(UC_ARM_REG_LR, END|1); u.reg_write(UC_ARM_REG_FPSCR, mode << 22)
        self.instructions = self.copies = self.copy_bytes = self.yields = 0
        u.emu_start(self.symbols[name]|1, END, count=10000000)
        assert u.reg_read(UC_ARM_REG_PC) == END, 'instruction ceiling'
        count = struct.unpack('<h', u.mem_read(CTX, 2))[0]
        geometry = bytearray()
        if count > 0:
            assert count <= 256
            for i in range(count*8):
                a = output+i
                geometry += u.mem_read(RAM+pages[a >> 12]+(a & 4095), 1)
        return dict(count=count, geometry=geometry.hex(), instructions=self.instructions,
                    copies=self.copies, copy_bytes=self.copy_bytes,
                    preempt=struct.unpack('<i',u.mem_read(CTX+self.layout['preempt'],4))[0])


def fixture(layout, case, mode):
    rng = random.Random(case)
    memory = bytearray(b'\xa5'*SIZE); context = bytearray(layout['size'])
    pages = [(i ^ 1)*4096 for i in range(SIZE//4096)]
    # Native geometry is word-aligned. The original lift's integer word moves
    # do not split a single unaligned word over independently mapped pages;
    # such input needs fallback, not a claimed equivalent typed admission.
    poly, boundary, output, sp = 0x18ff0+(case%4)*4, 0x38ff0+(case%4)*4, 0x58ff0+(case%4)*4, 0x91ff0
    def put(a, data):
        for j, v in enumerate(data): memory[pages[(a+j) >> 12]+((a+j) & 4095)] = v
    def word(a, value): put(a, struct.pack('<I', value))
    def fp(a, value): put(a, struct.pack('<f', value))
    n = [1, 2, 3, 4, 7, 14, 31, 64, 127, 255, 256][case % 11]
    edges = [1, 2, 3, 4, 6, 9, 16][case % 7]
    capacity = [1, 2, n, min(256, n+16), 256][case % 5]
    scale = [1e-7, 1e-4, .25, 1., 2.5, 10000.][case % 6]
    tolerance = [0., 9.999999747378752e-5, -.0001, 1/512][case % 4]
    for j in range(n):
        a = math.tau*j/n
        radius = scale * (1 + (rng.random()-.5)*.1)
        fp(poly+j*8, math.cos(a)*radius); fp(poly+j*8+4, math.sin(a)*radius)
    for j in range(edges):
        a = (-1 if case%9 == 0 else 1)*math.tau*j/edges
        fp(boundary+j*8, math.cos(a)); fp(boundary+j*8+4, math.sin(a))
    if edges > 1 and case%5 == 0:
        fp(boundary+8, 1); fp(boundary+12, 0)  # degenerate boundary edge
    fp(0x1f0a68, 0); fp(0x1f0a78, 1)
    put(0x1f0af8, struct.pack('<d', 9.999999747378752e-5))
    for j, v in enumerate((0x12345678, edges, boundary, capacity, output)): word(sp+j*4, v)
    fp(sp+20, tolerance)
    for j in range(8):
        struct.pack_into('<I', context, 4*j, 0xabc00000+j)
        struct.pack_into('<d', context, layout['st']+j*8, j+.375)
    for j, v in ((1,n), (2,poly), (4,sp)): struct.pack_into('<I',context,4*j,v)
    for name, value, fmt in [('fsp',case%8,'I'),('fsw',0xabcd,'H'),('fcw',0x37f,'H'),
                              ('preempt',100000,'I'),('f_bits',32,'I')]:
        struct.pack_into('<'+fmt, context, layout[name], value)
    return (bytes(memory), bytes(context), pages, mode), output, dict(n=n,edges=edges,
        capacity=capacity,scale=scale,tolerance=tolerance)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage', type=Path, required=True); p.add_argument('--xbe', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True); p.add_argument('--reuse', action='store_true')
    p.add_argument('--cases', type=int, default=128); p.add_argument('--mode', type=int)
    a = p.parse_args(); a.out = a.out.resolve()
    if a.out.is_relative_to(ROOT): p.error('owned outputs must stay outside source')
    a.out.mkdir(parents=True, exist_ok=a.reuse)
    elf = a.out/'test.elf' if a.reuse else build(a.stage.resolve(), a.xbe.resolve(), a.out)
    m = Machine(elf); rows = []
    for mode in ([a.mode] if a.mode is not None else range(16)):
        for case in range(a.cases):
            f, out, spec = fixture(m.layout, case, mode)
            original = m.run('f_000B7F10', f, out)
            candidate = m.run('test_candidate', f, out)
            row = dict(case=case,mode=mode,spec=spec,original=original,candidate=candidate)
            same = all(original[k] == candidate[k] for k in ('count','geometry','preempt'))
            rows.append(row)
            if not same:
                (a.out/'failure.json').write_text(json.dumps(row, indent=2)+'\n')
                raise AssertionError(f'geometry mismatch mode{mode} case{case}: '+str(spec))
        print('PASS mode', mode, 'cases', a.cases, flush=True)
    (a.out/'result.json').write_text(json.dumps(dict(fixtures=len(rows),rows=rows,
        limits=['Finite, word-aligned, disjoint input geometry; no production admission or scheduler qualification.',
                'Returned count, positive-count output bytes and remaining preemption budget are compared; callbacks do not fire.',
                'Modeled instructions and libc copies are not hardware time or FPS.']), indent=2)+'\n')
    print('PASS', len(rows), 'typed/original geometry comparisons')


if __name__ == '__main__': main()
