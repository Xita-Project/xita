#!/usr/bin/env python3
"""Compare the original sphere-plane block with Vita-compiled native arithmetic.

Requires a local supported XBE/manifest, VitaSDK, Unicorn and pyelftools. The
extracted original stays in the specified private output directory. Compares
the complete guest context, mapped memory and native FP status; instruction
counts exclude modeled memory copies and are not hardware cycles or FPS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys

from test_arm_model_palette import Machine, RAM, PT, STACK, CTX, END, SIZE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR,
                               UC_ARM_REG_PC, UC_ARM_REG_FPSCR)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery
from recompiler.core.hooks import NoGameHooks


class SphereMachine(Machine):
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
        assert uc.reg_read(UC_ARM_REG_FPSCR) == fpscr, 'ARM model did not retain requested FP controls'
        self.instructions = self.copies = self.copy_bytes = self.yields = 0
        uc.emu_start(self.symbols[function] | 1, END, count=100000)
        assert uc.reg_read(UC_ARM_REG_PC) == END
        return dict(context=bytes(uc.mem_read(CTX, self.layout['size'])),
                    memory=bytes(uc.mem_read(RAM, SIZE)),
                    fpscr=uc.reg_read(UC_ARM_REG_FPSCR),
                    accepted=uc.reg_read(UC_ARM_REG_R0),
                    instructions=self.instructions, copies=self.copies)


def fixture(layout, case, fpscr):
    rng = random.Random(87800 + case)
    memory = bytearray(b'\xa5' * SIZE)
    context = bytearray(layout['size'])
    pages = [(i ^ 0x40) * 4096 for i in range(SIZE // 4096)]
    # All byte alignments and noncontiguous guest-page crossings. Input-only
    # aliases are legal too; no pointer is replaced by a contiguous host span.
    point = 0x21ff8 + case % 16
    plane = 0x31ff4 + case % 16
    if case % 5 == 0:
        plane = point
    if case % 5 == 1:
        pages[0x31] = pages[0x21]
        pages[0x32] = pages[0x22]
    for i in range(8):
        struct.pack_into('<I', context, layout['r'] + i * 4, rng.getrandbits(32))
        struct.pack_into('<d', context, layout['st'] + i * 8, i + .375)
        for j in range(4):
            struct.pack_into('<f', context, layout['xmm'] + (i * 4 + j) * 4, i + j + .25)
    struct.pack_into('<I', context, layout['r'], 48)
    struct.pack_into('<I', context, layout['r'] + 4, point)
    struct.pack_into('<I', context, layout['r'] + 20, plane - 48)
    struct.pack_into('<I', context, layout['fsp'], case % 8)
    struct.pack_into('<H', context, layout['fsw'], 0xabcd)
    struct.pack_into('<H', context, layout['fcw'], 0x37f)
    edges = (0, 0x80000000, 1, 0x807fffff, 0x800000, 0x7f7fffff,
             0xff7fffff, 0x7f800000, 0xff800000, 0x7fc01234,
             0xffc05678, 0x7f801234, 0x3f800000, 0xbf800000)

    def write(address, word):
        for offset, value in enumerate(struct.pack('<I', word)):
            at = address + offset
            memory[pages[at >> 12] + (at & 4095)] = value

    for i in range(7):
        word = rng.getrandbits(32)
        if case % 3 == 0:
            word = (word & 0x807fffff) | ((110 + case % 30) << 23)
        elif case % 3 == 1:
            word = edges[(case + i) % len(edges)]
        write(plane + i * 4 if i < 4 else point + (i - 4) * 4, word)
    return bytes(memory), bytes(context), pages, fpscr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('ARM_CC', 'arm-vita-eabi-gcc'))
    args = parser.parse_args()
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=False)
    img = r.Image(str(args.xbe), str(args.manifest))
    assert HaloHooks(img).enabled, 'Unsupported executable'
    digest = hashlib.sha256(img.bytes_at(0x87ecc, 0x1a)).hexdigest()
    assert digest == '68ec0f334940aa871ae2fede180af8adf67f37fd58a0f094857397e9445970b0'
    disc = HaloDiscovery(img, {}, img.kernel_imports(), lambda *args: None)
    disc.add_root(0x87ea0)
    disc.lift_function(disc.functions[0x87ea0])
    disc.split_blocks(disc.functions[0x87ea0])
    emitter = r.Emitter(img, disc, {}, img.kernel_imports(), 'unused', 1, hooks=NoGameHooks())
    original = emitter.emit_function(disc.functions[0x87ea0])
    hooks = HaloHooks(img)
    patched = hooks.transform_body(0x87ea0, original)
    assert patched.count('if (!xv_bsp_sphere_plane_distance(c))') == 2
    read = img.bytes_at
    img.bytes_at = lambda address, size: bytes(size)
    try:
        assert hooks.transform_body(0x87ea0, original) == original
    finally:
        img.bytes_at = read
    assert hooks.transform_body(0x87e10, original) == original
    hooks.enabled = False
    assert hooks.transform_body(0x87ea0, original) == original
    blocks = re.findall(r'    /\* 00087ECC .*?(?=    /\* 00087EE6 )', original, re.S)
    assert len(blocks) == 2 and blocks[0] == blocks[1]
    reference = out / 'original.c'
    reference.write_text('#include "xv_x86rt.h"\nvoid original_sphere(xctx *c) {\n' + blocks[0] + '\n}\n')
    harness = out / 'harness.c'
    harness.write_text('''#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
const unsigned layout[] = {sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void test_boot(void) {}
void original_sphere(xctx *);
int xv_bsp_sphere_plane_distance(xctx *);
int candidate_sphere(xctx *c) {
 int accepted=xv_bsp_sphere_plane_distance(c);
 if(!accepted)original_sphere(c);
 return accepted;
}
void *memcpy(void *d,const void *s,size_t n) {(void)s;(void)n;return d;}
void *memmove(void *d,const void *s,size_t n) {(void)s;(void)n;return d;}
''')
    elf = out / 'sphere.elf'
    command = [args.cc, '-O2', '-std=gnu11', '-fno-strict-aliasing',
               '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon', '-I' + str(ROOT / 'recomp'),
               '-ffunction-sections', '-fdata-sections', str(reference), str(harness),
               str(ROOT / 'recomp/kernel/xk_geometry.c'), str(ROOT / 'recomp/xv_x86rt.c'),
               '-nostdlib', '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,'
               '--undefined=original_sphere,--undefined=candidate_sphere,--undefined=layout',
               '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True)
    machine = SphereMachine(elf)
    rows = []
    for mode in range(16):
        for case in range(96):
            sample = fixture(machine.layout, case, (mode << 22) | (0x9f if case & 1 else 0))
            a = machine.run('original_sphere', sample)
            b = machine.run('candidate_sphere', sample)
            for field in ('context', 'memory', 'fpscr'):
                if a[field] != b[field]:
                    path = out / f'mismatch-{mode}-{case}-{field}'
                    if field != 'fpscr':
                        path.with_suffix('.expected').write_bytes(a[field])
                        path.with_suffix('.actual').write_bytes(b[field])
                    raise AssertionError((str(path), a['fpscr'], b['fpscr']))
            assert b['accepted'] in (0, 1)
            rows.append(dict(mode=mode, case=case, accepted=b['accepted'],
                             original=a['instructions'], candidate=b['instructions']))
        print(f'PASS mode {mode}: {len(rows)} full-state comparisons', flush=True)
    # A decline itself must not touch guest state or raise a native exception.
    # Do not run the original with unmasked native traps in this fixture.
    trap_checked, trap_unavailable = [], []
    for controls in (0x100, 0x200, 0x400, 0x800, 0x1000, 0x8000):
        machine.uc.reg_write(UC_ARM_REG_FPSCR, controls)
        if machine.uc.reg_read(UC_ARM_REG_FPSCR) != controls:
            trap_unavailable.append(controls)
            continue  # Unicorn's Cortex-A9 model does not retain trap enables.
        sample = fixture(machine.layout, 12, controls)
        result = machine.run('xv_bsp_sphere_plane_distance', sample)
        assert result['accepted'] == 0
        assert result['memory'] == sample[0] and result['context'] == sample[1]
        assert result['fpscr'] == controls
        trap_checked.append(controls)
    report = dict(fixtures=len(rows), full_context_and_memory_match=True, fp_status_match=True,
                  version_guards_checked=True,
                  unmasked_control_declines_checked=trap_checked,
                  unmasked_controls_unavailable_in_emulator=trap_unavailable,
                  original_bytes_sha256=digest, elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                  command=command, rows=rows,
                  limitation='Instruction counts exclude modeled copies; not cycles or hardware FPS')
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(rows[0]))


if __name__ == '__main__':
    main()
