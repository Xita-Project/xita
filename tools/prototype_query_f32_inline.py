#!/usr/bin/env python3
"""Private selective-query primitive prototype; no production default changes.

The owned retained generic objects remain untouched. Compile the actual query
Make target before/after specializing a private include tree, then execute the
complete retained-query fixture with those exact objects. No guest code is
embedded here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
HELPERS = ('x87_load_f32', 'x87_store_f32')
HEADER = 'query_f32_primitives.h'
INCLUDE_DIR = 'query_f32_primitives'
INCLUDES = ('xv_recomp_protos.h', 'kernel/xk_collision_vertices.h',
            'kernel/xk_segment_sphere.h', 'kernel/xk_collision_traversal.h')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def selective_header(text):
    original = text
    for helper in HELPERS:
        pattern = r'static inline ([A-Za-z_][A-Za-z_0-9 *]*\b' + helper + r'\()'
        text, count = re.subn(pattern, r'static inline __attribute__((always_inline)) \1', text)
        if count != 1:
            raise ValueError('unexpected primitive declaration: ' + helper)
    restored = text
    for helper in HELPERS:
        pattern = r'static inline __attribute__\(\(always_inline\)\) ([A-Za-z_][A-Za-z_0-9 *]*\b' + helper + r'\()'
        restored, count = re.subn(pattern, r'static inline \1', restored)
        if count != 1:
            raise ValueError('primitive change is not reversible: ' + helper)
    if restored != original:
        raise ValueError('private header changed more than the two attributes')
    if '#define X_G(a)      ((void *)(g_xram + g_xpt[' not in text:
        raise ValueError('global mapping definition drift')
    return text


def specialize_unit(text, enabled):
    include = '#include "xv_recomp_protos.h"'
    if text.count(include) != 1 or text.index(include) > text.index('#undef X_G'):
        raise ValueError('query must parse global memory helpers before captured roots')
    if INCLUDE_DIR in text:
        raise ValueError('query already specialized')
    if not enabled:
        return text
    # The original uses pragma once (file identity), not a named include guard.
    # Copy its tiny include tree so every transitive ../xv_x86rt.h resolves to
    # the same private file. All other headers remain byte-identical.
    for name in INCLUDES:
        old = '#include "' + name + '"'
        if text.count(old) != 1 or text.index(old) > text.index('#undef X_G'):
            raise ValueError('unexpected query include ordering: ' + name)
        text = text.replace(old, '#include "' + INCLUDE_DIR + '/' + name + '"', 1)
    return text


def section(path, name):
    from elftools.elf.elffile import ELFFile
    with path.open('rb') as stream:
        s = ELFFile(stream).get_section_by_name(name)
        return s.data() if s else b''


def build(a):
    out = a.out.resolve()
    if out.is_relative_to(ROOT):
        raise ValueError('owned outputs must remain outside the source worktree')
    out.mkdir(parents=True, exist_ok=False)
    stage = out / 'build'
    stage.mkdir()
    files = subprocess.check_output(['git', 'ls-files'], cwd=ROOT, text=True).splitlines()
    for name in files:
        source = ROOT / name
        if source.is_file():
            target = stage / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    for name in ('code_013.c', 'code_016.c', 'code_028.c', 'xv_recomp_protos.h', 'xv_phase.h'):
        shutil.copy2(a.retained_build / 'recomp' / name, stage / 'recomp' / name)
    base = json.loads(a.build_command.read_text())['command'][:-1]
    base = [arg for arg in base if not arg.startswith(('XV_NATIVE_SOLVER_FUSION=', 'XBE=', 'XBE_JSON=', 'CC=', 'PREFIX=', 'VITASDK='))]
    cc = Path(a.arm_cc)
    base += ['XV_NATIVE_SOLVER_FUSION=0', 'VITASDK=' + str(cc.parent.parent),
             'CC=' + str(cc), 'PREFIX=' + str(cc).removesuffix('-gcc'),
             'XBE=' + str(a.xbe.resolve()), 'XBE_JSON=' + str(a.manifest.resolve()),
             'build/recomp/query_fusion.o']
    commands = []
    def make(lane):
        with (out / (lane + '-make.log')).open('w') as log:
            subprocess.run(base, cwd=stage, check=True, stdout=log, stderr=subprocess.STDOUT)
        commands.append(dict(command=base, cwd=str(stage), log=lane + '-make.log'))
        for suffix in ('.o', '.su'):
            source = stage / 'build/recomp' / ('query_fusion' + suffix)
            # Query production recipe does not otherwise request a stack file.
            if source.exists(): shutil.copy2(source, out / (lane + suffix))
    # CFLAGS from the real target remain authoritative; stack reporting is a
    # non-code-changing diagnostic added to the compiler invocation only.
    base.insert(-1, 'CC=' + str(cc) + ' -fstack-usage')
    make('baseline')
    source = stage / 'recomp/query_fusion.c'
    original = source.read_text()
    if section(out / 'baseline.o', '.text') != section(a.retained_build / 'build/recomp/query_fusion.o', '.text'):
        raise ValueError('current baseline does not match retained production query .text')
    header = (ROOT / 'recomp/xv_x86rt.h').read_text()
    private_header = selective_header(header)
    private = stage / 'recomp' / INCLUDE_DIR
    headers = list((stage / 'recomp').rglob('*.h'))
    for source_header in headers:
        relative = source_header.relative_to(stage / 'recomp')
        target = private / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source_header, target)
    (private / 'xv_x86rt.h').write_text(private_header)
    source.write_text(specialize_unit(original, True))
    make('inline')
    (out / 'baseline.c').write_text(original)
    (out / 'inline.c').write_text(source.read_text())
    (out / HEADER).write_text(private_header)
    # Final instruction/stack reports retain the real production imports.
    nm = str(cc).removesuffix('gcc') + 'nm'
    objdump = str(cc).removesuffix('gcc') + 'objdump'
    rows = {}
    for lane in ('baseline', 'inline'):
        obj = out / (lane + '.o')
        rows[lane] = dict(text_bytes=len(section(obj, '.text')), text_sha256=digest(section(obj, '.text')),
                          imports=subprocess.check_output([nm, '-u', str(obj)], text=True),
                          symbols=subprocess.check_output([nm, '-S', str(obj)], text=True),
                          stack=(out / (lane + '.su')).read_text())
        with (out / (lane + '.asm')).open('w') as f:
            subprocess.run([objdump, '-dr', str(obj)], stdout=f, check=True)
    (out / 'build.json').write_text(json.dumps(dict(commands=commands, results=rows,
        header_only_changed_attributes=list(HELPERS), global_roots_bound_before_capture=True,
        original_header_sha256=digest(header.encode()), private_header_sha256=digest(private_header.encode()),
        retained_baseline_text_identical=True), indent=2) + '\n')
    return out


def execute(a, out):
    from test_arm_cluster_runtime import RuntimeMachine, RAM, SIZE, STACK
    from unicorn.arm_const import UC_ARM_REG_SP
    saved = json.loads((a.fixture_dir / 'commands.json').read_text())
    cc = str(a.arm_cc)
    flags = saved[0][1:saved[0].index('-c')]
    # Reuse the exact saved fixture objects and original generic objects; the
    # fixture is unchanged by selective inlining in the separate query unit.
    bridge = out / 'bridge.c'
    bridge.write_text('#include "xv_x86rt.h"\n'
        'void nq_query_at_171f94(xctx *,unsigned);\n'
        'void f_00087EA0(xctx *);void f_00087E10(xctx *);\n'
        'void candidate_00088110(xctx *c){nq_query_at_171f94(c,0x172c95u);}\n'
        'void candidate_00087EA0(xctx *c){f_00087EA0(c);}\n'
        'void candidate_00087E10(xctx *c){f_00087E10(c);}\n')
    commands = [[cc, *flags, '-c', str(bridge), '-o', str(out / 'bridge.o')]]
    subprocess.run(commands[0], check=True)
    for lane in ('reference', 'baseline', 'inline'):
        aliases = ['--defsym=original_' + addr + '=f_' + addr for addr in ('00088110','00087EA0','00087E10')]
        if lane == 'reference': aliases += ['--defsym=candidate_' + addr + '=f_' + addr for addr in ('00088110','00087EA0','00087E10')]
        link = saved[-1]
        ldflags = link[link.index('-nostdlib'):]
        ldflags[-1] = str(out / (lane + '.elf'))
        ldflags.insert(1, '-Wl,--unresolved-symbols=ignore-all,' + ','.join(aliases))
        objects = [a.retained_build / 'build/recomp' / ('code_' + n + '.o') for n in ('013','016')]
        objects += [a.fixture_dir / (str(n) + '.o') for n in range(1,7)]
        if lane != 'reference': objects += [out / (lane + '.o'), out / 'bridge.o']
        command = [cc, *flags, *map(str, objects), *ldflags]
        subprocess.run(command, check=True); commands.append(command)
    (out / 'execution-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    class Machine(RuntimeMachine):
        def step(self, uc, address, size, user):
            self.min_sp = min(self.min_sp, uc.reg_read(UC_ARM_REG_SP))
            super().step(uc, address, size, user)
        def call(self, *args, **kw):
            self.min_sp = 0xffffffff
            stats = super().call(*args, **kw)
            stats['peak_stack_bytes'] = STACK + 65024 - self.min_sp
            return stats
    machines = {lane: Machine(out / (lane + '.elf'), profile=True) for lane in ('reference','baseline','inline')}
    for m in machines.values(): m.imports = {k:v for k,v in m.imports.items() if v != '__wrap_xv_preempt'}
    def state(m):
        result = dict(context=bytes(m.uc.mem_read(m.context, m.layout['size'])), memory=bytes(m.uc.mem_read(RAM,SIZE)))
        for name, size in [('ct_yields',4),('ct_events',4),('ct_seen',4),('ct_boundary',4),('ct_original_pages',4096),('ct_alternate_pages',4096)]:
            result[name] = bytes(m.uc.mem_read(m.symbols[name], size))
        root = struct.unpack('<I', m.uc.mem_read(m.symbols['g_xpt'], 4))[0]
        result['root'] = 'original' if root == m.symbols['ct_original_pages'] else 'alternate' if root == m.symbols['ct_alternate_pages'] else root
        return result
    cases = [(d,v,100000,0) for d in (1,16) for v in (0,3,4,8,12,15,1<<25)]
    rows = []
    for depth, variant, budget, fpscr in cases:
        expected = None; result = {}
        for lane, m in machines.items():
            m.call('arm_prepare',(depth,variant,budget))
            stats = m.call('arm_original' if lane == 'reference' else 'arm_candidate', fpscr=fpscr)
            current = state(m)
            if expected is None: expected = current; expected_fp = stats['fpscr']
            checks = {key:current[key] == value for key,value in expected.items()}
            checks['fpscr'] = stats['fpscr'] == expected_fp
            if not all(checks.values()): raise AssertionError((lane, depth, variant, checks))
            result[lane] = dict(stats=stats, checks=checks)
        rows.append(dict(depth=depth, variant=variant, budget=budget, fpscr=fpscr, lanes=result))
        (out / 'result.json').write_text(json.dumps(rows, indent=2) + '\n')
        print('PASS full query',depth,variant,{lane:(r['stats']['instructions'],r['stats']['peak_stack_bytes']) for lane,r in result.items()},flush=True)


def main():
    if not __debug__: raise SystemExit('Refusing optimized Python')
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('retained-build','build-command','fixture-dir','out','xbe','manifest'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--arm-cc', type=Path, default=Path('/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc'))
    a = p.parse_args()
    out = build(a)
    execute(a, out)


if __name__ == '__main__': main()
