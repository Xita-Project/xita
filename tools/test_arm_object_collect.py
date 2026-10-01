#!/usr/bin/env python3
"""Cortex-A9 differential and instruction-count test for xk_object_collect.c.

The oracle is the complete original 0x171F10, 0x1716F0 and 0x1D130, lifted
from the locally owned XBE with production translation-unit mapping macros.
The candidate is the hooked 0x171F10 emission plus the unmodified helper. All
units use the Makefile's recomp flags with VitaSDK. Three lanes run from each
identical fixture: translated reference, helper enabled and helper disabled.

Callees outside the lifted set (0x88110, 0x868F0, 0x487E0, 0x855F0, 0x81900,
0x81770, 0x172DE0, 0x172F40), xv_preempt and libc copies are modeled in Python
and excluded from instruction counts. Each lane must present identical context,
arguments and walk frame window at every modeled call and yield, and finish with
identical context, arena and FPSCR. Generated original code is written only to
--output-dir. Instruction counts are neither CPU cycles nor Vita frame time.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR,
    UC_CPU_ARM_CORTEX_A9, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks
from recompiler import xita_recomp as r
from test_object_collect import lift, LIFTED, STUBS

RAM, PT, STACK_HOST, CTX, END, ENV = (0x20000000, 0x21000000, 0x22000000,
                                      0x23000000, 0x24000000, 0x25000000)
ARENA = 8 << 20
PAGES = ARENA >> 12
WORLD, STACK, ALIAS = 0x500000, 0x680000, 0x4F0
NONE = 0xFFFFFFFF
MASK = 0xFFFFFFFF
XK_SUB = 3
FIELDS = ('size', 'r', 'st', 'fsp', 'fsw', 'fcw', 'preempt', 'f_kind', 'f_op1', 'f_op2',
          'f_res', 'f_bits', 'f_cf_override', 'f_of_override', 'f_cf', 'f_of')
RECOMP_CFLAGS = ['-O2', '-fno-strict-aliasing', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon',
                 '-w', '-std=gnu11']
PREAMBLE = '''#undef X_G
#define X_G(a) ((void *)(xram_ + xpt_[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu)))
#undef X_IMG8
#undef X_IMG16
#undef X_IMG32
#define X_IMG8(a)  (*(uint8_t *)(imgb_ + (uint32_t)(a)))
#define X_IMG16(a) (*(xu16_u  *)(imgb_ + (uint32_t)(a)))
#define X_IMG32(a) (*(xu32_u  *)(imgb_ + (uint32_t)(a)))
'''
# (site, argument words hashed, callee stack bytes popped, local fill range)
STUB_MODEL = {
    0x88110: (2, 8, None), 0x868F0: (5, 0x14, None), 0x855F0: (6, 0x18, None),
    0x81770: (5, 0x14, None), 0x172F40: (5, 0x14, None),
    0x487E0: (1, 4, (0x1C, 0x7C)), 0x81900: (1, 4, (0x1C, 0x7C)),
    0x172DE0: (0, 0, (0x18, 0x78)),
}
FPSCR_MODES = (0, 1 << 22, 2 << 22, 3 << 22, 0x9F, 0x01000000, 0x02000000, 0x0300009F)


def generate(args, out):
    img = r.Image(args.xbe, args.manifest)
    hooks = HaloHooks(img)
    assert hooks.object_collect_enabled, 'unsupported executable'
    bodies = {entry: lift(img, entry, NoGameHooks()) for entry in LIFTED}
    candidate = lift(img, 0x171F10, hooks)
    assert candidate.count('xv_object_collect_refs(c)') == 1
    text = '#include "xv_x86rt.h"\n' + ''.join(
        f'void f_{a:08X}(xctx *);\n' for a in STUBS + (0x1716F0, 0x1D130))
    text += 'void reference_00171F10(xctx *);\nvoid candidate_00171F10(xctx *);\n' + PREAMBLE
    text += bodies[0x1D130] + bodies[0x1716F0]
    text += bodies[0x171F10].replace('void f_00171F10(', 'void reference_00171F10(', 1)
    text += candidate.replace('void f_00171F10(', 'void candidate_00171F10(', 1)
    reference = out / 'original-private.c'
    reference.write_text(text)
    return reference


def build(out, reference, cc, helper):
    fixture = out / 'arm-object-collect-fixture.c'
    stubs = ''.join(f'void f_{a:08X}(xctx *c) {{ (void)c; __builtin_trap(); }}\n' for a in STUBS)
    fixture.write_text('''#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
const unsigned layout[] = {sizeof(xctx), offsetof(xctx,r), offsetof(xctx,st), offsetof(xctx,fsp),
    offsetof(xctx,fsw), offsetof(xctx,fcw), offsetof(xctx,preempt), offsetof(xctx,f_kind),
    offsetof(xctx,f_op1), offsetof(xctx,f_op2), offsetof(xctx,f_res), offsetof(xctx,f_bits),
    offsetof(xctx,f_cf_override), offsetof(xctx,f_of_override), offsetof(xctx,f_cf), offsetof(xctx,f_of)};
void test_boot(void) {}
void xk_os_log(const char *format, ...) { (void)format; __builtin_trap(); }
char *getenv(const char *name) { (void)name; __builtin_trap(); }
int atoi(const char *value) { (void)value; __builtin_trap(); }
void __wrap_xv_preempt(xctx *c) { (void)c; __builtin_trap(); }
void __wrap_xv_trap(xctx *c, uint32_t eip) { (void)c; (void)eip; __builtin_trap(); }
void *memcpy(void *d, const void *s, size_t n) { (void)s; (void)n; __builtin_trap(); }
void *memmove(void *d, const void *s, size_t n) { (void)s; (void)n; __builtin_trap(); }
void *memset(void *d, int v, size_t n) { (void)v; (void)n; __builtin_trap(); }
''' + stubs)
    elf = out / 'arm-object-collect.elf'
    common = [cc] + RECOMP_CFLAGS + ['-DXV_NATIVE_OBJECT_COLLECT', '-ffunction-sections',
              '-fdata-sections', '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel')]
    commands, objects = [], []
    for index, source in enumerate((reference, fixture, helper,
                                   ROOT / 'recomp/xv_x86rt.c')):
        obj = out / f'unit-{index}.o'
        command = common + ['-c', str(source), '-o', str(obj)]
        subprocess.run(command, check=True)
        commands.append(command)
        objects.append(str(obj))
    command = common + objects + ['-nostdlib',
        '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,--wrap=xv_trap,'
        '--undefined=reference_00171F10,--undefined=candidate_00171F10,--undefined=layout',
        '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True)
    commands.append(command)
    return elf, commands


def page_base(page, alias_page):
    if page == ALIAS and alias_page is not None:
        return alias_page
    return ((page ^ 1) if page < PAGES else PAGES - 1) * 4096


class World:
    """Guest arena with the host test's permuted pages and optional alias page."""
    def __init__(self):
        self.m = bytearray(b'\x5a' * (ARENA + 4096))
        self.m[(PAGES - 1) * 4096:] = b'\xff' * 8192
        self.alias_page = None
        self.leaf_list, self.bsp_result = [], 1
        self.object_total, self.salt, self.mutate = 0, 0, False
        self.preempt = 1000

    def phys(self, address):
        address &= MASK
        return page_base(address >> 12, self.alias_page) + (address & 4095)

    def write(self, address, data):
        for i, byte in enumerate(data):
            self.m[self.phys(address + i)] = byte

    def w32(self, address, value): self.write(address, struct.pack('<I', value & MASK))
    def w16(self, address, value): self.write(address, struct.pack('<H', value & 0xFFFF))
    def w8(self, address, value): self.write(address, bytes([value & 255]))
    def wf(self, address, value): self.write(address, struct.pack('<f', value))
    def img32(self, address, value): struct.pack_into('<I', self.m, address, value & MASK)
    def img16(self, address, value): struct.pack_into('<H', self.m, address, value & 0xFFFF)


def skeleton(rng, entry):
    """Globals, table headers and constants shared by all fixtures."""
    w = World()
    walk = entry - 0x1024
    w.walk = walk
    for a in range(walk + 0x14, walk + 0x200):
        w.w8(a, 0xFF)
    base = WORLD + rng.randrange(1024) * 4
    lay = dict(walk=walk, entry=entry, bsp=base, leaves=base + 0x100, heads=base + 0x2000,
               ref_header=base + 0x3000, ref_data=base + 0x3100, flag_block=base + 0x6F00,
               center=base + 0x6F80, packet=base + 0x6FC0, object_header=base + 0x7000,
               object_data=base + 0x7100, objects=base + 0x10000)
    w.img32(0x39BE58, lay['bsp']); w.w32(lay['bsp'] + 0xE4, lay['leaves'])
    w.img32(0x2FC6A0, lay['heads']); w.img32(0x2FC6A4, lay['ref_header'])
    w.w32(lay['ref_header'] + 0x34, lay['ref_data'])
    w.img32(0x2FC6AC, lay['object_header']); w.w32(lay['object_header'] + 0x34, lay['object_data'])
    w.img32(0x278248, lay['flag_block'])
    w.img32(0x39BE54, rng.getrandbits(32)); w.img32(0x27824C, rng.getrandbits(32))
    w.img16(0x1F845C, rng.randrange(64))
    w.wf(0x1F0F38, 0.5)
    for t, value in enumerate((0, 1, 2, 2, 2, 2, 1, 1, 1)):
        w.w8(0x171944 + t, value)
    w.salt = rng.randrange(0xFFFF)
    return w, lay


def finish_frame(w, lay, rng, flags, center, radius, ignore, epoch, object_epoch):
    entry = lay['entry']
    w.img32(0x2D2FAC, epoch); w.img32(0x2FC684, object_epoch)
    w.w32(entry, rng.getrandbits(32)); w.w32(entry + 4, flags); w.w32(entry + 8, center)
    w.wf(entry + 12, radius); w.w32(entry + 16, rng.getrandbits(32))
    w.w32(entry + 20, rng.getrandbits(32)); w.w32(entry + 24, ignore)
    w.w32(entry + 28, lay['packet']); w.w32(lay['packet'], rng.getrandbits(32))


def put_object(w, lay, index, spec):
    """spec: bounds (x,y,z,r) floats or raw words, type, word, b6, stamp, sibling, child."""
    obj = spec.get('address', lay['objects'] + index * 0x440)
    w.w32(lay['object_data'] + index * 12 + 8, obj)
    if 'address' not in spec:
        for a in range(obj, obj + 0x430, 4):
            w.w32(a, (index * 2654435761 + a) & MASK)
    w.w32(obj + 4, spec.get('word', 0x00000100))
    w.w32(obj + 8, spec.get('stamp', 0x1234))
    for k, value in enumerate(spec.get('bounds', (0.0, 0.0, 0.0, 1.0))):
        if isinstance(value, float):
            w.wf(obj + 0x50 + k * 4, value)
        else:
            w.w32(obj + 0x50 + k * 4, value)
    w.w16(obj + 0x64, spec.get('type', 9))
    w.w8(obj + 0xB6, spec.get('b6', 0))
    w.w32(obj + 0xC4, spec.get('sibling', NONE))
    w.w32(obj + 0xC8, spec.get('child', NONE))
    return obj


def scenario(name, rng):
    """Small designed walks. Every chain object in leaf cluster 0, unstamped."""
    entry = STACK + 0x800
    w, lay = skeleton(rng, entry)
    epoch, object_epoch = 0x1000, 0x2000
    flags = 0x0001FF80                      # objects, all type bits 0..8
    specs, chain = [], []
    miss = dict(bounds=(100.0, 0.0, 0.0, 1.0))
    hit = dict(bounds=(0.5, 0.0, 0.0, 1.0))
    datum = lambda i: i | w.salt << 16
    stamped_leaf = False
    if name == 'stamped-leaf':
        specs = [dict(miss)]; stamped_leaf = True
    elif name == 'stamped-object':
        specs = [dict(miss, stamp=object_epoch + 1)]
    elif name == 'reject-sphere-1':
        specs = [dict(miss)]
    elif name == 'reject-flags-1':
        specs = [dict(hit, word=0x101)]
    elif name == 'reject-selector-1':
        specs = [dict(hit, type=3)]
    elif name == 'reject-equal-boundary-1':
        specs = [dict(bounds=(3.0, 0.0, 4.0, 4.5), type=9)]
    elif name == 'fallback-biped-1':
        specs = [dict(hit, type=0)]
    elif name == 'fallback-scenery-1':
        specs = [dict(hit, type=6)]
    elif name == 'fallback-sibling-1':
        specs = [dict(miss, sibling=datum(1)), dict(miss)]
        chain = [0]
    elif name == 'nonfinite-bound-1':
        specs = [dict(bounds=(0.5, 0x7FC00000, 0.0, 1.0))]
    elif name == 'infinite-bound-1':
        specs = [dict(bounds=(0x7F800000, 0.0, 0.0, 1.0))]
    elif name == 'subnormal-bound-1':
        specs = [dict(bounds=(0x00000001, 0x80000003, 0.0, 1.0))]
    elif name in ('rejects-16', 'rejects-64'):
        specs = [dict(bounds=(100.0 + i, 0.0, 0.0, 1.0)) for i in range(int(name.split('-')[1]))]
    elif name in ('mixed-128', 'yields-mixed-32'):
        count = 128 if name == 'mixed-128' else 32
        for i in range(count):
            roll = rng.randrange(20)
            spec = dict(miss) if roll < 13 else dict(hit, type=rng.choice((2, 3, 9))) if roll < 17 \
                else dict(hit, type=rng.choice((0, 6)))
            specs.append(spec)
        if name.startswith('yields'):
            w.mutate, w.preempt = True, 1
    elif name == 'alias-center-80':
        specs = [dict(hit, type=9) for _ in range(8)]
    elif name == 'alias-physical-object':
        specs = [dict(miss) for _ in range(4)]
    else:
        raise ValueError(name)
    if not chain:
        chain = list(range(len(specs)))
    w.object_total = len(specs)
    for i, spec in enumerate(specs):
        put_object(w, lay, i, spec)
    for j, index in enumerate(chain):
        link = lay['ref_data'] + j * 12
        w.w32(link + 4, datum(index))
        w.w32(link + 8, datum(j + 1) if j + 1 < len(chain) else NONE)
    w.w32(lay['heads'], datum(0))
    w.w16(lay['leaves'] + 8, 0)
    w.w32(0x2D2FB0, epoch + 1 if stamped_leaf else 0x55)
    w.leaf_list, w.bsp_result = [0], 1
    center = lay['center']
    if name == 'alias-center-80':
        center = lay['walk'] - 0x80
    if name == 'alias-physical-object':
        target = lay['walk'] - 0x60
        w.alias_page = page_base(target >> 12, None)
        w.w32(lay['object_data'] + 8, ALIAS * 4096 + (target & 4095))
    else:
        for k in range(3):
            w.wf(center + k * 4, 0.0)
    finish_frame(w, lay, rng, flags, center, 0.0, 0x7FFF0000, epoch, object_epoch)
    return w


def random_world(rng):
    """Port of tools/tests/object_collect.c build() without regression kinds."""
    entry = STACK + (rng.randrange(4096) & ~3)
    w, lay = skeleton(rng, entry)
    walk = lay['walk']
    alias = rng.randrange(16)
    clusters, top, children = 1 + rng.randrange(12), rng.randrange(160), rng.randrange(24)
    total = top + children
    leaves_in_bsp = 1 + rng.randrange(320)
    datum = lambda i: i | w.salt << 16
    epoch, object_epoch = rng.getrandbits(32), rng.getrandbits(32)
    for i in range(leaves_in_bsp):
        w.w16(lay['leaves'] + i * 16 + 8, rng.randrange(clusters))
    for k in range(clusters):
        w.w32(0x2D2FB0 + k * 4, (epoch + 1) & MASK if rng.randrange(4) == 0 else rng.getrandbits(32))
    used = 0
    for k in range(clusters):
        length = rng.randrange(40) if top else 0
        length = min(length, 400 - used)
        w.w32(lay['heads'] + k * 4, datum(used) if length else NONE)
        for j in range(length):
            link = lay['ref_data'] + used * 12
            w.w32(link, rng.getrandbits(32))
            w.w32(link + 4, datum(rng.randrange(top)) if rng.randrange(50) else NONE)
            w.w32(link + 8, datum(used + 1) if j + 1 < length else NONE)
            used += 1
    types = (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0, 1, 6, 23, -8, -1, 0x7FFF)
    odd = (0x7F800000, 0xFF800000, 0x7FC00000, 0x7F7FFFFF, 0x00000001)
    for i in range(total):
        word = rng.getrandbits(32)
        if rng.randrange(8): word &= ~1
        if rng.randrange(8): word &= ~0x1000000
        bounds = [rng.randrange(-10, 11) * 0.5 for _ in range(3)] + [rng.randrange(9) * 0.5]
        if rng.randrange(40) == 0:
            bounds[rng.randrange(4)] = rng.choice(odd)
        sibling = child = NONE
        if i >= top and i + 1 < total and rng.randrange(3): sibling = datum(i + 1)
        if i < top and children and rng.randrange(10) == 0: sibling = datum(top + rng.randrange(children))
        if children and rng.randrange(16) == 0:
            first = top if i < top else i + 1
            if first < total: child = datum(first + rng.randrange(total - first))
        put_object(w, lay, i, dict(word=word, bounds=bounds, type=rng.choice(types),
                   b6=rng.getrandbits(8), sibling=sibling, child=child,
                   stamp=rng.getrandbits(32) if rng.randrange(6) else (object_epoch + 1) & MASK))
    for t in range(9):
        if rng.randrange(10) == 0:
            w.w8(0x171944 + t, rng.randrange(3))
    center = lay['center']
    below = lambda low, high: walk - (low + rng.randrange((high - low) // 4 + 1) * 4)
    if alias == 1:
        center = below(0x04, 0x98)
    if alias == 2:
        target = below(0x04, 0x98)
        w.alias_page = page_base(target >> 12, None)
        center = ALIAS * 4096 + (target & 4095)
    if alias == 3 and top:
        w.w32(lay['object_data'] + rng.randrange(top) * 12 + 8, below(0x10, 0xB0))
    if alias == 4 and top:
        target = below(0x10, 0xB0)
        w.alias_page = page_base(target >> 12, None)
        w.w32(lay['object_data'] + rng.randrange(top) * 12 + 8, ALIAS * 4096 + (target & 4095))
    if alias not in (1, 2):
        for k in range(3):
            w.wf(center + k * 4, rng.randrange(-10, 11) * 0.5)
    leaf_count = 0 if rng.randrange(9) == 0 else rng.randrange(300)
    w.leaf_list = [rng.randrange(leaves_in_bsp) | (rng.getrandbits(1) << 31) for _ in range(leaf_count)]
    w.bsp_result = int(rng.randrange(8) != 0)
    flags = rng.getrandbits(32)
    if rng.randrange(8): flags |= 0x80
    if rng.randrange(5) == 0: flags &= ~0xFFF00
    ignore = datum(rng.randrange(top)) if top and rng.randrange(6) == 0 else rng.getrandbits(32)
    finish_frame(w, lay, rng, flags, center, rng.randrange(13) * 0.5, ignore, epoch, object_epoch)
    w.object_total, w.mutate = total, True
    w.preempt = rng.randrange(80) - 2
    return w


class Machine:
    def __init__(self, elf_path):
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        uc.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
        uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
        with elf_path.open('rb') as file:
            elf = ELFFile(file)
            self.symbols = {s.name: s['st_value'] for s in
                            elf.get_section_by_name('.symtab').iter_symbols() if s.name}
            segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
            for page in sorted({p for s in segments for p in range(
                    s['p_vaddr'] & ~4095, (s['p_vaddr'] + s['p_memsz'] + 4095) & ~4095, 4096)}):
                uc.mem_map(page, 4096)
            for segment in segments:
                uc.mem_write(segment['p_vaddr'], segment.data())
        for base, size in ((RAM, ARENA + 8192), (PT, 4 << 20), (STACK_HOST, 1 << 20),
                           (CTX, 4096), (END, 4096), (ENV, 4096)):
            uc.mem_map(base, size)
        uc.mem_write(ENV, b'XV_NATIVE_OBJECT_COLLECT\0')
        uc.mem_write(ENV + 64, b'1\0')
        for name, value in (('g_xram', RAM), ('g_img_base', RAM), ('g_xpt', PT)):
            uc.mem_write(self.symbols[name], struct.pack('<I', value))
        self.layout = dict(zip(FIELDS, struct.unpack('<' + 'I' * len(FIELDS),
                                                     uc.mem_read(self.symbols['layout'], 4 * len(FIELDS)))))
        self.pt = b''.join(struct.pack('<I', page_base(i, None)) for i in range(1 << 20))
        uc.mem_write(PT, self.pt)
        self.count = 0
        uc.hook_add(UC_HOOK_CODE, self.tick)
        self.models = {}
        for name in ('getenv', 'atoi', 'xk_os_log', 'memcpy', 'memmove', 'memset',
                     '__wrap_xv_preempt', '__wrap_xv_trap'):
            if name in self.symbols:
                self.models[self.symbols[name] & ~1] = name
        for site in STUB_MODEL:
            name = f'f_{site:08X}'
            if name in self.symbols:
                self.models[self.symbols[name] & ~1] = site
        for address in self.models:
            uc.hook_add(UC_HOOK_CODE, self.model, begin=address, end=address)
        self.world = None
        self.env = True

    def tick(self, uc, address, size, user):
        self.count += 1

    # Guest access with the generated code's unguarded 4-byte behavior.
    def gread(self, address, size):
        return bytes(self.uc.mem_read(RAM + self.world.phys(address), size))

    def gwrite(self, address, data):
        self.uc.mem_write(RAM + self.world.phys(address), data)

    def context(self):
        return bytearray(self.uc.mem_read(CTX, self.layout['size']))

    def field(self, ctx, name, value=None, fmt='<I', index=0):
        offset = self.layout[name] + index * struct.calcsize(fmt)
        if value is None:
            return struct.unpack_from(fmt, ctx, offset)[0]
        struct.pack_into(fmt, ctx, offset, value)

    def record(self, site, words):
        ctx = self.context()
        esp = self.field(ctx, 'r', index=4)
        frame = b''.join(self.gread(esp + 4 * i, 4) for i in range(words + 1))
        walk = self.world.walk
        window = b''.join(self.gread(walk - 0xA0 + i, 1) for i in range(0xC0))
        digest = hashlib.blake2b(struct.pack('<I', site) + bytes(ctx) + frame + window,
                                 digest_size=8).digest()
        self.events.append((site, digest))
        return ctx, int.from_bytes(digest, 'little')

    def model(self, uc, address, size, user):
        kind = self.models[address]
        self.count -= 1
        self.modeled += 1
        lr = uc.reg_read(UC_ARM_REG_LR)
        if kind == 'getenv':
            key = bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 64)).split(b'\0')[0]
            uc.reg_write(UC_ARM_REG_R0, ENV + 64 if key == b'XV_NATIVE_OBJECT_COLLECT' and self.env else 0)
        elif kind == 'atoi':
            uc.reg_write(UC_ARM_REG_R0, uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 1)[0] - 48)
        elif kind in ('memcpy', 'memmove', 'memset'):
            dst, src, n = (uc.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
            assert n <= 1 << 20
            data = bytes([src & 255]) * n if kind == 'memset' else bytes(uc.mem_read(src, n))
            uc.mem_write(dst, data)
            self.copies += 1
        elif kind == '__wrap_xv_preempt':
            ctx, h = self.record(NONE, 0)
            self.yields += 1
            self.field(ctx, 'preempt', 1 + h % 40, '<i')
            policy = (h >> 40) % 4 if self.world.mutate else 0
            if policy:
                self.field(ctx, 'f_cf', (h >> 11) & 1); self.field(ctx, 'f_of', (h >> 13) & 1)
                self.field(ctx, 'r', (h >> 17) & MASK, index=1)
                if self.field(ctx, 'f_kind') == XK_SUB and self.field(ctx, 'f_op2') == NONE:
                    if policy == 1:
                        self.field(ctx, 'r', NONE, index=0)
                    else:
                        self.field(ctx, 'r', (h >> 23) & MASK, index=0)
                        if policy == 3 and self.world.object_total:
                            self.field(ctx, 'r', (h % self.world.object_total) | self.world.salt << 16, index=2)
                else:
                    self.field(ctx, 'r', h % max(len(self.world.leaf_list), 1), index=0)
                    self.field(ctx, 'r', (h >> 29) & MASK, index=2)
            uc.mem_write(CTX, bytes(ctx))
        elif kind == '__wrap_xv_trap':
            raise AssertionError('guest trap')
        elif kind == 'xk_os_log':
            pass
        else:
            words, popped, fill = STUB_MODEL[kind]
            ctx, h = self.record(kind, words)
            esp = self.field(ctx, 'r', index=4)
            if kind == 0x88110:
                out = self.field(ctx, 'r', index=6)
                self.gwrite(out + 0xC0C, struct.pack('<I', len(self.world.leaf_list)))
                for i, leaf in enumerate(self.world.leaf_list):
                    self.gwrite(out + 0xC10 + i * 4, struct.pack('<I', leaf))
            if fill:
                for a in range(esp + fill[0], esp + fill[1]):
                    self.gwrite(a, bytes([((h >> (a & 31)) ^ a) & 255]))
            self.field(ctx, 'r', h & MASK, index=0)
            self.field(ctx, 'r', (h >> 32) & MASK, index=1)
            self.field(ctx, 'r', (h >> 16) & MASK, index=2)
            for name, value in (('f_kind', 0), ('f_op1', 0), ('f_op2', 0), ('f_res', (h >> 8) & MASK),
                                ('f_bits', 32), ('f_cf_override', 0), ('f_of_override', 0),
                                ('f_cf', (h >> 3) & 1), ('f_of', (h >> 5) & 1)):
                self.field(ctx, name, value)
            fsp = self.field(ctx, 'fsp')
            self.field(ctx, 'st', (h & 0xFFFF) / 7.0, '<d', (fsp - 1) & 7)
            fsw = self.field(ctx, 'fsw', fmt='<H')
            self.field(ctx, 'fsw', (fsw & ~0x4700) | ((h >> 20) & 0x4500), '<H')
            self.field(ctx, 'r', (esp + 4 + popped) & MASK, index=4)
            if kind == 0x88110:
                r0 = self.field(ctx, 'r', index=0)
                self.field(ctx, 'r', (r0 & ~255) | self.world.bsp_result, index=0)
            uc.mem_write(CTX, bytes(ctx))
        uc.reg_write(UC_ARM_REG_PC, lr)

    def tally(self):
        names = ('collect_walks', 'collect_leaves', 'collect_objects', 'collect_skipped')
        return {n: struct.unpack('<I', self.uc.mem_read(self.symbols[n], 4))[0] for n in names}

    def run(self, function, world, context, fpscr, active, env=True):
        uc = self.uc
        self.world, self.env = world, env
        uc.mem_write(RAM, bytes(world.m))
        alias = world.alias_page if world.alias_page is not None else page_base(ALIAS, None)
        uc.mem_write(PT + ALIAS * 4, struct.pack('<I', alias))
        uc.mem_write(CTX, context)
        uc.mem_write(self.symbols['collect_active'], struct.pack('<i', active))
        before = self.tally()
        uc.reg_write(UC_ARM_REG_R0, CTX)
        uc.reg_write(UC_ARM_REG_SP, STACK_HOST + (1 << 20) - 256)
        uc.reg_write(UC_ARM_REG_LR, END | 1)
        uc.reg_write(UC_ARM_REG_FPSCR, fpscr)
        self.count = self.modeled = self.copies = self.yields = 0
        self.events = []
        uc.emu_start(self.symbols[function] | 1, END, count=60_000_000)
        assert uc.reg_read(UC_ARM_REG_PC) == END, function + ' did not return'
        after = self.tally()
        return dict(context=bytes(uc.mem_read(CTX, self.layout['size'])),
                    memory=bytes(uc.mem_read(RAM, ARENA + 8)), fpscr=uc.reg_read(UC_ARM_REG_FPSCR),
                    events=list(self.events), instructions=self.count, copies=self.copies,
                    yields=self.yields, modeled=self.modeled,
                    **{k[8:]: (after[k] - before[k]) & MASK for k in after})


def initial_context(machine, rng, world):
    layout = machine.layout
    ctx = bytearray(rng.getrandbits(8) for _ in range(layout['size']))
    walk = world.walk
    struct.pack_into('<I', ctx, layout['r'] + 16, walk + 0x1024)
    for name, value in (('f_kind', rng.randrange(6)), ('f_bits', rng.choice((8, 16, 32))),
                        ('f_cf_override', rng.randrange(2)), ('f_of_override', rng.randrange(2)),
                        ('f_cf', rng.randrange(2)), ('f_of', rng.randrange(2)),
                        ('fsp', rng.randrange(8))):
        struct.pack_into('<I', ctx, layout[name], value)
    struct.pack_into('<H', ctx, layout['fcw'], 0x37F)
    struct.pack_into('<i', ctx, layout['preempt'], world.preempt)
    return bytes(ctx)


NZCV = 0xF0000000


def compare(out, label, row, reference, other, strict_nzcv):
    # FPSCR NZCV holds the last VFP comparison result. Runtime consumers always
    # compare immediately before `vmrs APSR_nzcv`; it is counted separately.
    row['nzcv_' + label.rsplit('-', 1)[1]] = (reference['fpscr'] ^ other['fpscr']) & NZCV != 0
    for field in ('events', 'context', 'memory', 'fpscr'):
        mask = MASK if strict_nzcv or field != 'fpscr' else ~NZCV & MASK
        differs = (reference[field] & mask) != (other[field] & mask) if field == 'fpscr' \
            else reference[field] != other[field]
        if differs:
            base = out / f'mismatch-{label}-{field}'
            base.with_suffix('.json').write_text(json.dumps(row, indent=2, default=str))
            if field in ('context', 'memory'):
                base.with_suffix('.expected').write_bytes(reference[field])
                base.with_suffix('.actual').write_bytes(other[field])
            if field == 'events':
                n = next((i for i, (a, b) in enumerate(zip(reference['events'], other['events'])) if a != b),
                         min(len(reference['events']), len(other['events'])))
                raise AssertionError((label, row, 'first differing event', n,
                                      len(reference['events']), len(other['events'])))
            detail = (hex(reference['fpscr']), hex(other['fpscr'])) if field == 'fpscr' else str(base)
            raise AssertionError((label, row, field, detail))


SCENARIOS = ('stamped-leaf', 'stamped-object', 'reject-sphere-1', 'reject-flags-1',
             'reject-selector-1', 'reject-equal-boundary-1', 'fallback-biped-1',
             'fallback-scenery-1', 'fallback-sibling-1', 'nonfinite-bound-1', 'infinite-bound-1',
             'subnormal-bound-1', 'rejects-16', 'rejects-64', 'mixed-128', 'yields-mixed-32',
             'alias-center-80', 'alias-physical-object')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', required=True)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('ARM_CC', 'arm-vita-eabi-gcc'))
    parser.add_argument('--random-cases', type=int, default=400)
    parser.add_argument('--helper', type=Path, default=ROOT / 'recomp/kernel/xk_object_collect.c')
    parser.add_argument('--strict-nzcv', action='store_true', help='also require equal FPSCR NZCV')
    args = parser.parse_args()
    out = args.output_dir
    out.mkdir(parents=True, exist_ok=True)
    reference = generate(args, out)
    elf, commands = build(out, reference, args.cc, args.helper)
    machine = Machine(elf)
    rows = []

    # One-time environment resolution with the actual configuration path.
    rng = random.Random(171)
    world = scenario('reject-sphere-1', rng)
    ctx = initial_context(machine, rng, world)
    first = machine.run('candidate_00171F10', world, ctx, 0, -1)
    steady = machine.run('candidate_00171F10', world, ctx, 0, 1)
    resolution = dict(first_call_instructions=first['instructions'], first_call_modeled=first['modeled'],
                      steady_instructions=steady['instructions'])
    assert struct.unpack('<i', machine.uc.mem_read(machine.symbols['collect_active'], 4))[0] == 1

    unsupported = []
    for mask in (0x100, 0x200, 0x400, 0x800, 0x1000, 0x8000):
        machine.uc.reg_write(UC_ARM_REG_FPSCR, mask)
        if machine.uc.reg_read(UC_ARM_REG_FPSCR) != mask:
            unsupported.append(mask)

    def execute(label, world, fpscr, seed):
        rng = random.Random(seed)
        ctx = initial_context(machine, rng, world)
        ref = machine.run('reference_00171F10', world, ctx, fpscr, 1)
        on = machine.run('candidate_00171F10', world, ctx, fpscr, 1)
        off = machine.run('candidate_00171F10', world, ctx, fpscr, 0)
        row = dict(label=label, fpscr=fpscr, seed=seed)
        compare(out, label + '-on', row, ref, on, args.strict_nzcv)
        compare(out, label + '-off', row, ref, off, args.strict_nzcv)
        assert off['walks'] == 0 and ref['walks'] == 0
        row.update(reference=ref['instructions'], on=on['instructions'], off=off['instructions'],
                   walks=on['walks'], leaves=on['leaves'], objects=on['objects'], skipped=on['skipped'],
                   yields=ref['yields'], modeled_events=len(ref['events']), copies=[ref['copies'], on['copies']])
        rows.append(row)
        return row

    for index, name in enumerate(SCENARIOS):
        for mode in FPSCR_MODES:
            world = scenario(name, random.Random(1000 + index))
            execute(name, world, mode, 5000 + index * 16 + FPSCR_MODES.index(mode))
        print('PASS ARM scenario', name, 'reference', rows[-8]['reference'], 'on', rows[-8]['on'],
              'off', rows[-8]['off'], 'objects', rows[-8]['objects'], 'skipped', rows[-8]['skipped'], flush=True)
    random_rows = []
    for case in range(args.random_cases):
        rng = random.Random(90000 + case)
        world = random_world(rng)
        random_rows.append(execute('random', world, FPSCR_MODES[case % len(FPSCR_MODES)], 70000 + case))
        if case % 50 == 49:
            print('PASS ARM random', case + 1, flush=True)

    def summary(selected):
        walked = [x for x in selected if x['walks']]
        return dict(cases=len(selected), walked=len(walked),
                    reference=sum(x['reference'] for x in walked), on=sum(x['on'] for x in walked),
                    off=sum(x['off'] for x in walked), objects=sum(x['objects'] for x in walked),
                    skipped=sum(x['skipped'] for x in walked),
                    on_losses=sum(1 for x in walked if x['on'] > x['reference']),
                    worst_on_loss=max([x['on'] - x['reference'] for x in walked] + [0]),
                    off_losses=sum(1 for x in selected if x['off'] > x['reference']),
                    worst_off_loss=max([x['off'] - x['reference'] for x in selected] + [0]))

    scenarios = {name: {k: v for k, v in next(x for x in rows if x['label'] == name and x['fpscr'] == 0).items()
                        if k in ('reference', 'on', 'off', 'objects', 'skipped', 'walks', 'yields')}
                 for name in SCENARIOS}
    report = dict(
        result='PASS', oracle='original 171F10/1716F0/1D130 lifted from owned XBE, production mapping macros',
        fixtures=len(rows), random=summary(random_rows), scenarios=scenarios, strict_nzcv=args.strict_nzcv,
        nzcv_differences={lane: sum(1 for x in rows if x.get('nzcv_' + lane)) for lane in ('on', 'off')},
        configuration=resolution, fpscr_modes=FPSCR_MODES,
        unsupported_native_trap_controls=unsupported,
        compared='modeled-call/yield context, arguments and walk frame window; final context, arena, FPSCR',
        limits=['ARM instructions in Unicorn Cortex-A9, not cycles, cache effects or Vita frame time',
                'modeled callees, yields and libc copies excluded from counts',
                'native trap-enable FPSCR bits are read-as-zero in this emulator'],
        sources={str(p): hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in (
            args.helper, ROOT / 'games/halo_ce_3925/hooks.py',
            ROOT / 'recomp/xv_x86rt.c', ROOT / 'recomp/xv_x86rt.h', Path(__file__).resolve())},
        original_private_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
        elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(), commands=commands)
    (out / 'cases.json').write_text(json.dumps(rows, indent=2) + '\n')
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('fixtures', 'random', 'scenarios', 'configuration')}, indent=2))


if __name__ == '__main__':
    main()
