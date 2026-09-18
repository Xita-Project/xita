#!/usr/bin/env python3
"""Qualify the optional 0xB0CB0 segment/sphere body against the owned XBE.

The oracle is the original function as lifted from the owned image into a
private staged generated unit (--staged-code); its instruction span and the
image hash are checked. The candidate is produced by the game
profile's own function-entry and body hooks applied to that original, exactly
as the emitter inserts them after the captured roots. Both are compiled in one
private unit with the production captured-root mapping macros. Generated code
stays in --out, outside the repository.

Host mode (optionally ASan/UBSan) compares context, the complete fixture arena,
yield observations, native floating-point exception flags and admissions over
four rounding modes. --arm builds with VitaSDK using the Makefile recomp flags
and runs Unicorn Cortex-A9 lanes (original, candidate ON, candidate OFF),
comparing context, arena, yield observations and the complete FPSCR, and
counting executed ARM instructions outside the fixture's own callback code.
Instruction counts are not CPU cycles or Vita frame time.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery
from games.halo_ce_3925 import segment_sphere

IMAGE = '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
PREAMBLE = '''#undef X_G
#define X_G(a) ((void *)(xram_ + xpt_[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu)))
#undef X_IMG8
#undef X_IMG16
#undef X_IMG32
#define X_IMG8(a)  (*(uint8_t *)(imgb_ + (uint32_t)(a)))
#define X_IMG16(a) (*(xu16_u  *)(imgb_ + (uint32_t)(a)))
#define X_IMG32(a) (*(xu32_u  *)(imgb_ + (uint32_t)(a)))
'''
RECOMP_CFLAGS = ['-O2', '-fno-strict-aliasing', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon', '-w', '-std=gnu11']
ARM_FPSCR = (0, 1 << 22, 2 << 22, 3 << 22, 0x9F, 0x01000000, 0x02000000, 0x0300009F,
             0xF0000000, 0x60000000 | (2 << 22), 0x0340009F | 0x90000000, 0x01C00010)
# Negative controls: each single-defect header copy must fail the comparison.
MUTATIONS = {
    'status-top-cleared': ('(fsw & ~0x4700u)', '(fsw & ~0x7F00u)'),
    'inside-slot': ('        c->st[(top - 5u) & 7u] = rr;\n        c->fsw = fsw;', '        c->st[(top - 5u) & 7u] = q;\n        c->fsw = fsw;'),
    'no-yield': ('        X_PREEMPT();\n', ''),
    'no-radius-spill': ('    x_guest_write(s + 0x14u, &t32f, 4);\n', ''),
    'leaving-flags': ('        xv_ss_flags(c, 0u);', '        xv_ss_flags(c, 1u);'),
    'direction-read-before-spill': ('    const float q32 = (float)q;\n    x_guest_write(s, &q32, 4);',
                                    '    const uint32_t early_ = X_M32(direction);\n    const float q32 = (float)q;\n'
                                    '    x_guest_write(s, &q32, 4);\n    if (early_ != X_M32(direction)) X_M32(s + 0x10u) ^= 1u;'),
    'admit-nan': ('    return (word & 0x7F800000u) == 0x7F800000u && (word & 0x007FFFFFu) != 0;', '    return 0;'),
    'no-page-check': ('    if ((s & 4095u) > 4096u - 0x18u) return 0;\n', ''),
}


def mutated_header(out, name):
    text = (ROOT / 'recomp/kernel/xk_segment_sphere.h').read_text()
    old, new = MUTATIONS[name]
    assert text.count(old) == 1, name
    directory = out / 'mutant' / 'kernel'
    directory.mkdir(parents=True, exist_ok=True)
    (directory / 'xk_segment_sphere.h').write_text(
        text.replace(old, new).replace('#include "../xv_x86rt.h"', '#include "xv_x86rt.h"'))
    return ['-I' + str(out / 'mutant')]


class OwnedImage:
    """Minimal hash-checked image view for the game profile's span checks."""
    def __init__(self, xbe, manifest):
        self.data = Path(xbe).read_bytes()
        m = json.loads(Path(manifest).read_text())
        self.sections = [(s['virtual_address'], s['raw_address'], s['raw_size']) for s in m['sections']]

    def bytes_at(self, address, size):
        for va, raw, raw_size in self.sections:
            if va <= address < va + raw_size:
                return self.data[raw + address - va:raw + address - va + size]
        return b''


PROLOGUE = '    uint8_t *const imgb_ = g_img_base; (void)imgb_;\n'


def hook_emission(image, hooks, original):
    """Insert function-entry lines after the captured roots, then transform."""
    assert original.count(PROLOGUE) == 1
    entry = hooks.function_entry(0xB0CB0)
    body = original.replace(PROLOGUE, PROLOGUE + ''.join(line + '\n' for line in entry), 1)
    return hooks.transform_body(0xB0CB0, body)


def staged_body(args):
    text = Path(args.staged_code).read_text()
    match = re.search(r'^void f_000B0CB0\(xctx \*restrict c\)\n\{\n.*?^\}\n', text, re.M | re.S)
    assert match, 'staged unit lacks f_000B0CB0'
    return match[0].rstrip('\n')


def fresh_lift(args):
    """Independently discover, lift and emit 0xB0CB0 from the owned image."""
    from recompiler import xita_recomp as r
    image = r.Image(str(args.xbe), str(args.manifest))
    assert hashlib.sha256(image.data).hexdigest() == IMAGE
    discovery = HaloDiscovery(image, {}, image.kernel_imports(), lambda *a: None)
    discovery.add_root(0xB0CB0)
    function = discovery.functions[0xB0CB0]
    discovery.lift_function(function)
    discovery.split_blocks(function)
    return r.Emitter(image, discovery, {}, image.kernel_imports(), 'unused', 1,
                     hooks=NoGameHooks()).emit_function(function)


def original_body(args):
    staged = staged_body(args)
    if not args.lift:
        return staged, None
    lifted = fresh_lift(args)
    (args.out / 'f_000B0CB0-fresh-lift-private.c').write_text(lifted + '\n')
    check = dict(fresh_lift_sha256=hashlib.sha256(lifted.encode()).hexdigest(),
                 staged_sha256=hashlib.sha256(staged.encode()).hexdigest(), identical=lifted == staged)
    if not check['identical']:
        (args.out / 'lift-mismatch.json').write_text(json.dumps(check, indent=2) + '\n')
        raise SystemExit('fresh lift differs from staged oracle; see lift-mismatch.json')
    return lifted, check


def generate(args):
    if not __debug__:
        raise SystemExit('assertions are required')
    image = OwnedImage(args.xbe, args.manifest)
    assert hashlib.sha256(image.data).hexdigest() == IMAGE, 'unsupported image'
    assert segment_sphere.matches(image)
    original, lift_check = original_body(args)
    assert not re.search(r'\bf_[0-9A-F]{8}\(c\)', original), 'unexpected call in B0CB0'
    assert original.count('X_PREEMPT()') == 1 and original.count('L_000B0CFC:') == 1
    # Every emitted instruction comment must name the original bytes' instructions in address order.
    addresses = [int(a, 16) for a in re.findall(r'/\* ([0-9A-F]{8})  ', original)]
    assert addresses and min(addresses) == 0xB0CB0 and max(addresses) == 0xB0DC0 and len(set(addresses)) == 93, \
        'staged body span'
    hooks = HaloHooks(image)
    candidate = hook_emission(image, hooks, original)
    assert hook_emission(image, NoGameHooks(), original) == original
    assert candidate.count('xv_segment_sphere(c, xram_, xpt_)') == 1
    assert candidate.startswith('#ifdef XV_NATIVE_SEGMENT_SPHERE\n#include "kernel/xk_segment_sphere.h"')
    # Span drift removes both the entry lines and the header.
    read = image.bytes_at
    image.bytes_at = lambda a, n: bytes(n) if (a, n) == segment_sphere.SPAN[:2] else read(a, n)
    try:
        assert not segment_sphere.matches(image)
        assert hook_emission(image, HaloHooks(image), original) == original, 'signature drift retained the hook'
    finally:
        image.bytes_at = read
    assert all('segment_sphere' not in line for line in hooks.function_entry(0xB0CCC)), 'interior entry hooked'
    out = args.out
    (out / 'f_000B0CB0-original-private.c').write_text(original + '\n')
    (out / 'f_000B0CB0-candidate-private.c').write_text(candidate + '\n')
    pp = []
    for name, text in (('off-original', original), ('off-candidate', candidate)):
        path = out / (name + '-private.c')
        path.write_text('#include "xv_x86rt.h"\n' + text)
        pp.append(subprocess.check_output([os.environ.get('CC', 'cc'), '-E', '-P', '-I' + str(ROOT / 'recomp'), str(path)]))
    assert pp[0] == pp[1], 'compile-OFF function changed'
    unit = out / 'segment-sphere-unit-private.c'
    unit.write_text('#include "xv_x86rt.h"\n' + PREAMBLE +
                    original.replace('void f_000B0CB0(', 'void reference_000B0CB0(', 1) + '\n' +
                    candidate.replace('void f_000B0CB0(', 'void candidate_000B0CB0(', 1) + '\n')
    oracle = dict(image_sha256=IMAGE, span=[hex(segment_sphere.SPAN[0]), segment_sphere.SPAN[1], segment_sphere.SPAN[2]],
                  original_source=str(args.staged_code),
                  original_source_sha256=hashlib.sha256(Path(args.staged_code).read_bytes()).hexdigest(),
                  original_function_sha256=hashlib.sha256(original.encode()).hexdigest(),
                  candidate_function_sha256=hashlib.sha256(candidate.encode()).hexdigest(),
                  unit_sha256=hashlib.sha256(unit.read_bytes()).hexdigest(), compile_off_identical=True,
                  fresh_lift=lift_check)
    (out / 'oracle.json').write_text(json.dumps(oracle, indent=2) + '\n')
    return unit, oracle


def sources():
    names = ('recomp/kernel/xk_segment_sphere.h', 'recomp/kernel/xk_segment_sphere_control.c',
             'games/halo_ce_3925/segment_sphere.py', 'games/halo_ce_3925/hooks.py',
             'tools/test_segment_sphere.py', 'tools/tests/segment_sphere.c', 'recomp/xv_x86rt.h', 'recomp/xv_x86rt.c')
    return {n: hashlib.sha256((ROOT / n).read_bytes()).hexdigest() for n in names}


def host(args, unit, oracle):
    cc = os.environ.get('CC', 'cc')
    sanitize = ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer', '-no-pie'] if args.sanitize else ['-O2']
    mutant = mutated_header(args.out, args.mutation) if args.mutation else []
    command = [cc, *sanitize, '-std=gnu11', '-fno-strict-aliasing', '-frounding-math', '-DXV_NATIVE_SEGMENT_SPHERE',
               '-ffunction-sections', '-fdata-sections', *mutant,
               '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel'), str(unit),
               str(ROOT / 'tools/tests/segment_sphere.c'), str(ROOT / 'recomp/kernel/xk_segment_sphere_control.c'),
               str(ROOT / 'recomp/xv_x86rt.c'), '-Wl,--gc-sections,--wrap=xv_preempt,--wrap=xv_trap', '-lm',
               '-o', str(args.out / 'test')]
    if args.startup is not None:
        command[1:1]=['-DSEGMENT_SPHERE_STARTUP='+str(args.startup),
                      '-DXV_NATIVE_SEGMENT_SPHERE_DEFAULT='+str(args.startup)]
    subprocess.run(command, check=True)
    run = subprocess.run([str(args.out / 'test'), str(args.cases)] + (['state-only'] if args.state_only else []),
                         capture_output=True, text=True, timeout=3600)
    sys.stdout.write(run.stdout)
    sys.stderr.write(run.stderr[-4000:])
    receipt = dict(mode='host-asan-ubsan' if args.sanitize else 'host', mutation=args.mutation, state_only=args.state_only, returncode=run.returncode,
                   stdout=run.stdout, stderr=run.stderr[-4000:], command=command, cases=args.cases,
                   oracle=oracle, sources=sources())
    receipt['startup_mode']=args.startup
    (args.out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    if args.mutation:
        if not run.returncode:
            raise SystemExit('FAIL negative control passed: ' + args.mutation)
        print('PASS negative control detected:', args.mutation)
    elif run.returncode:
        raise SystemExit('FAIL host segment/sphere')


def arm(args, unit, oracle):
    cc = args.cc
    out = args.out
    common = [cc] + RECOMP_CFLAGS + ['-DXV_NATIVE_SEGMENT_SPHERE', '-DTEST_ARM', '-ffunction-sections', '-fdata-sections',
                                     '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel')]
    commands, objects = [], []
    units = (unit, ROOT / 'tools/tests/segment_sphere.c', ROOT / 'recomp/kernel/xk_segment_sphere_control.c',
             ROOT / 'recomp/xv_x86rt.c', ROOT / 'tools/tests/cluster_runtime_arm_imports.c')
    for index, source in enumerate(units):
        obj = out / f'arm-unit-{index}.o'
        command = common + ['-c', str(source), '-o', str(obj)]
        subprocess.run(command, check=True)
        commands.append(command)
        objects.append(obj)
    elf_path = out / 'segment-sphere.elf'
    names = ('reference_000B0CB0', 'candidate_000B0CB0', 'seg_prepare', 'seg_ctx', 'seg_arena', 'layout',
             'xv_segment_sphere_mode', 'xv_segment_sphere_count')
    command = common + [str(o) for o in objects] + ['-nostdlib', '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,'
               '--wrap=xv_preempt,--wrap=xv_trap,' + ','.join('--undefined=' + n for n in names), '-lc', '-lgcc',
               '-o', str(elf_path)]
    subprocess.run(command, check=True)
    commands.append(command)
    # Production-shaped object: generated unit with the captured-root preamble.
    size = subprocess.run([str(Path(cc).with_name('arm-vita-eabi-size')), '-A', str(objects[0])],
                          capture_output=True, text=True, check=True).stdout
    try:
        from elftools.elf.elffile import ELFFile
        from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
        from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_FPSCR, UC_CPU_ARM_CORTEX_A9,
                                       UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC)
    except ImportError as error:
        receipt = dict(mode='arm-build-only', result='BUILT, NOT EXECUTED', reason=str(error),
                       unit_object_sections=size, commands=commands, oracle=oracle, sources=sources(),
                       elf_sha256=hashlib.sha256(elf_path.read_bytes()).hexdigest())
        (out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
        raise SystemExit('ARM build and link passed; instruction execution requires unicorn and pyelftools: ' + str(error))

    with objects[1].open('rb') as file:
        fixture_functions = {s.name for s in ELFFile(file).get_section_by_name('.symtab').iter_symbols()
                             if s['st_info']['type'] == 'STT_FUNC' and s['st_shndx'] != 'SHN_UNDEF'}
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
    uc.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
    uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
    with elf_path.open('rb') as file:
        elf = ELFFile(file)
        symbols, sizes, excluded = {}, {}, []
        for s in elf.get_section_by_name('.symtab').iter_symbols():
            if s.name:
                symbols[s.name] = s['st_value']
                sizes[s.name] = s['st_size']
                if s['st_info']['type'] == 'STT_FUNC' and s.name in fixture_functions and s['st_size']:
                    excluded.append((s['st_value'] & ~1, (s['st_value'] & ~1) + s['st_size']))
        segments = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        for page in sorted({p for s in segments for p in range(s['p_vaddr'] & ~4095,
                                                            (s['p_vaddr'] + s['p_memsz'] + 4095) & ~4095, 4096)}):
            uc.mem_map(page, 4096)
        for s in segments:
            uc.mem_write(s['p_vaddr'], s.data())
    STACK, END = 0x70000000, 0x71000000
    uc.mem_map(STACK, 1 << 20)
    uc.mem_map(END, 4096)
    layout = dict(zip(('size', 'r', 'st', 'fsp', 'fsw', 'preempt'),
                      struct.unpack('<6I', uc.mem_read(symbols['layout'], 24))))
    arena_size = 64 * 4096 + 8
    counter = [0]
    copies = [0]
    firmware = {symbols[n] & ~1: n for n in ('sceClibMemcpy', 'sceClibMemmove', 'sceClibMemset') if n in symbols}

    def model(uc_, address, size, user):
        # Firmware copies: performed here, excluded from instruction counts.
        from unicorn.arm_const import UC_ARM_REG_R1, UC_ARM_REG_R2
        dst, src, n = (uc_.reg_read(x) for x in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2))
        assert n <= 1 << 22
        data = bytes([src & 255]) * n if firmware[address] == 'sceClibMemset' else bytes(uc_.mem_read(src, n))
        uc_.mem_write(dst, data)
        copies[0] += 1
        uc_.reg_write(UC_ARM_REG_R0, dst)
        uc_.reg_write(UC_ARM_REG_PC, uc_.reg_read(UC_ARM_REG_LR))

    for address in firmware:
        uc.hook_add(UC_HOOK_CODE, model, begin=address, end=address)
        excluded.append((address, address + 2))

    def tick(uc_, address, size, user):
        for lo, hi in excluded:
            if lo <= address < hi:
                return
        counter[0] += 1

    def call(name, r0, fpscr=0, count=False):
        uc.reg_write(UC_ARM_REG_R0, r0)
        uc.reg_write(UC_ARM_REG_SP, STACK + (1 << 20) - 64)
        uc.reg_write(UC_ARM_REG_LR, END | 1)
        uc.reg_write(UC_ARM_REG_FPSCR, fpscr)
        handle = uc.hook_add(UC_HOOK_CODE, tick) if count else None
        counter[0] = 0
        copies[0] = 0
        try:
            uc.emu_start(symbols[name] | 1, END, count=200_000_000)
        finally:
            if handle is not None:
                uc.hook_del(handle)
        assert uc.reg_read(UC_ARM_REG_PC) == END, name
        return counter[0], uc.reg_read(UC_ARM_REG_FPSCR), copies[0]

    # Trap-enable model: Unicorn keeps FPSCR trap enables read-as-zero, so OR
    # a mask into the register produced by the candidate's admission VMRS.
    trap_mask, trap_hits = [0], [0]
    if args.trap_model:
        from unicorn import arm_const
        listing = subprocess.run([str(Path(cc).with_name('arm-vita-eabi-objdump')), '-d', '--no-show-raw-insn',
                                  '--disassemble=candidate_000B0CB0', str(elf_path)],
                                 capture_output=True, text=True, check=True).stdout.splitlines()
        sites = [i for i, line in enumerate(listing) if re.search(r'\tvmrs\tr\d+, fpscr', line)]
        assert len(sites) == 1, 'expected one admission FPSCR read'
        register = int(re.search(r'vmrs\tr(\d+)', listing[sites[0]]).group(1))
        after = int(listing[sites[0] + 1].split(':')[0], 16)
        reg_id = getattr(arm_const, f'UC_ARM_REG_R{register}')

        def inject(uc_, address, size, user):
            uc_.reg_write(reg_id, uc_.reg_read(reg_id) | trap_mask[0])
            trap_hits[0] += 1
        uc.hook_add(UC_HOOK_CODE, inject, begin=after, end=after)
        excluded.append((after, after + 1))

    u32 = lambda name: struct.unpack('<I', uc.mem_read(symbols[name], 4))[0]
    put32 = lambda name, value: uc.mem_write(symbols[name], struct.pack('<I', value))
    ctx_addr, arena_addr = symbols['seg_ctx'], symbols['seg_arena']
    trap_controls = []
    for mask in (0x100, 0x200, 0x400, 0x800, 0x1000, 0x8000):
        uc.reg_write(UC_ARM_REG_FPSCR, mask)
        if uc.reg_read(UC_ARM_REG_FPSCR) != mask:
            trap_controls.append(mask)
    rows = []
    for seed in range(args.first_seed, args.first_seed + args.cases):
        fpscr = ARM_FPSCR[seed % len(ARM_FPSCR)] if args.fpscr is None else args.fpscr
        call('seg_prepare', seed)
        arena = bytes(uc.mem_read(arena_addr, arena_size))
        ctx = bytes(uc.mem_read(ctx_addr, layout['size']))
        admit, klass = u32('seg_expect_admit'), u32('seg_class')
        results = {}
        for lane, name, mode in (('original', 'reference_000B0CB0', 0), ('on', 'candidate_000B0CB0', 1),
                                 ('off', 'candidate_000B0CB0', 0)):
            uc.mem_write(arena_addr, arena)
            uc.mem_write(ctx_addr, ctx)
            put32('xv_segment_sphere_mode', mode)
            put32('xv_segment_sphere_count', 0)
            put32('seg_yields', 0)
            put32('seg_events', 2166136261)
            trap_mask[0] = (0x100, 0x200, 0x400, 0x800, 0x1000, 0x8000)[seed % 6] if args.trap_model and lane == 'on' else 0
            hits_before = trap_hits[0]
            instructions, final_fpscr, copied = call(name, ctx_addr, fpscr, count=True)
            if args.trap_model and lane == 'on':
                assert trap_hits[0] == hits_before + 1, 'trap model did not reach the admission check'
            results[lane] = dict(instructions=instructions, fpscr=final_fpscr, copies=copied,
                                 context=bytes(uc.mem_read(ctx_addr, layout['size'])),
                                 arena=bytes(uc.mem_read(arena_addr, arena_size)),
                                 yields=u32('seg_yields'), events=u32('seg_events'),
                                 admitted=u32('xv_segment_sphere_count'))
        ref = results['original']
        row = dict(seed=seed, fpscr=fpscr, admit=admit, klass=klass, yields=ref['yields'],
                   copies=[results[x]['copies'] for x in ('original', 'on', 'off')],
                   original=ref['instructions'], on=results['on']['instructions'], off=results['off']['instructions'])
        for lane in ('on', 'off'):
            for field in ('context', 'arena', 'fpscr', 'yields', 'events'):
                if results[lane][field] != ref[field]:
                    detail = (hex(ref['fpscr']), hex(results[lane]['fpscr'])) if field == 'fpscr' else field
                    if field in ('context', 'arena'):
                        a, b = ref[field], results[lane][field]
                        row['differing_offsets'] = [dict(offset=hex(i), expected=a[i], actual=b[i])
                                                    for i in range(len(a)) if a[i] != b[i]][:64]
                        (out / f'failure-{lane}-{field}-expected.bin').write_bytes(a)
                        (out / f'failure-{lane}-{field}-actual.bin').write_bytes(b)
                        (out / 'failure-input-arena.bin').write_bytes(arena)
                        (out / 'failure-input-context.bin').write_bytes(ctx)
                        row['context_layout'] = layout
                    (out / 'failure.json').write_text(json.dumps(row, indent=2) + '\n')
                    raise AssertionError((lane, row, field, detail))
        expected_on = 0 if args.trap_model else admit
        assert results['on']['admitted'] == expected_on and results['off']['admitted'] == 0, row
        rows.append(row)
        if seed % 500 == 499:
            print('PASS ARM segment/sphere', seed + 1, flush=True)

    def summary(selected):
        return dict(cases=len(selected), original=sum(x['original'] for x in selected),
                    on=sum(x['on'] for x in selected), off=sum(x['off'] for x in selected),
                    on_losses=sum(1 for x in selected if x['on'] > x['original']),
                    worst_on_loss=max([x['on'] - x['original'] for x in selected] + [0]),
                    median_original=sorted(x['original'] for x in selected)[len(selected) // 2] if selected else 0,
                    median_on=sorted(x['on'] for x in selected)[len(selected) // 2] if selected else 0,
                    median_off=sorted(x['off'] for x in selected)[len(selected) // 2] if selected else 0)
    names = {0: 'declined', 1: 'inside', 2: 'leaving', 3: 'no_root', 4: 'behind_yield_path', 5: 'intersects',
             6: 'nan_direction_continuation'}
    report = dict(mode='arm-cortex-a9-trap-model' if args.trap_model else 'arm-cortex-a9', result='PASS',
                  cases=len(rows), fpscr_modes=ARM_FPSCR, trap_model_hits=trap_hits[0],
                  by_class={names[k]: summary([x for x in rows if x['klass'] == k]) for k in names},
                  all=summary(rows), yields=sum(x['yields'] for x in rows),
                  unsupported_trap_controls=trap_controls,
                  text_bytes={n: sizes.get(n) for n in ('reference_000B0CB0', 'candidate_000B0CB0')},
                  limits=['ARM instructions in Unicorn Cortex-A9, not cycles or Vita frame time',
                          'fixture callback functions excluded from counts; libc and runtime helpers counted',
                          'native trap-enable bits are read-as-zero in the emulator'],
                  commands=commands, oracle=oracle, sources=sources(),
                  elf_sha256=hashlib.sha256(elf_path.read_bytes()).hexdigest())
    (out / 'cases.json').write_text(json.dumps(rows) + '\n')
    (out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ('cases', 'by_class', 'all', 'yields', 'text_bytes')}, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--staged-code', type=Path, required=True,
                   help='private generated unit containing the original f_000B0CB0')
    p.add_argument('--cases', type=int, default=20000)
    p.add_argument('--first-seed', type=int, default=0, help='ARM: first fixture seed')
    p.add_argument('--fpscr', type=lambda v: int(v, 0), help='ARM: force one FPSCR value')
    p.add_argument('--trap-model', action='store_true', help='ARM: inject trap enables at the admission read; expect declines')
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--arm', action='store_true')
    p.add_argument('--startup', type=int, choices=(0,1), help='Host: fixed process-start mode without any mode writes or counter resets')
    p.add_argument('--lift', action='store_true', help='regenerate the original from the owned image and require it to match --staged-code')
    p.add_argument('--mutation', choices=sorted(MUTATIONS), help='host negative control')
    p.add_argument('--state-only', action='store_true', help='skip the expected-admission check (negative controls)')
    p.add_argument('--cc', default=os.environ.get('ARM_CC', 'arm-vita-eabi-gcc'))
    args = p.parse_args()
    if args.startup is not None and args.arm:
        p.error('--startup uses the host generated-function fixture')
    args.out.mkdir(parents=True, exist_ok=True)
    unit, oracle = generate(args)
    (arm if args.arm else host)(args, unit, oracle)


if __name__ == '__main__':
    main()
