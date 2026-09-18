#!/usr/bin/env python3
"""Bounded composition oracle for owned model hierarchy and palette regions.

Generated guest code and ARM receipts must stay in a private output directory.
The arithmetic units are linked unchanged from an existing retained build. ARM
serial guard imports are modeled; the companion worker fixture uses real locks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as recomp
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery
import test_model_hierarchy as hierarchy_source
import test_model_palette as palette_source
import test_arm_model_hierarchy as hierarchy
import test_arm_model_palette as base
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR


def generate(xbe, manifest, directory):
    image = recomp.Image(str(xbe), str(manifest))
    hooks = HaloHooks(image)
    if not (hooks.hierarchy_enabled and hooks.palette_enabled):
        raise RuntimeError('Owned image does not match both audited regions')
    discovery = HaloDiscovery(image, {}, image.kernel_imports(), lambda *a: None)
    for address in (0x8DDF0, 0xA26B0, 0xB5B40, 0xB5F60):
        discovery.add_root(address)
        discovery.lift_function(discovery.functions[address])
        discovery.split_blocks(discovery.functions[address])
    original = recomp.Emitter(image, discovery, {}, image.kernel_imports(), 'unused', 1, hooks=NoGameHooks())
    candidate = recomp.Emitter(image, discovery, {}, image.kernel_imports(), 'unused', 1, hooks=hooks)
    source = '#include "xv_x86rt.h"\n'
    for address, name in ((0xB5B40, 'matrix'), (0xB5F60, 'quaternion')):
        source += original.emit_function(discovery.functions[address]).replace(f'f_{address:08X}', 'original_' + name)
        source += candidate.emit_function(discovery.functions[address])
    for address, name, extract in ((0x8DDF0, 'hierarchy', hierarchy_source.region), (0xA26B0, 'palette', palette_source.region)):
        raw = extract(original.emit_function(discovery.functions[address]), 'current_' + name)
        source += raw.replace('current_' + name, 'original_' + name).replace('f_000B5B40', 'original_matrix').replace('f_000B5F60', 'original_quaternion')
        source += raw
        source += extract(candidate.emit_function(discovery.functions[address]), 'candidate_' + name)
    path = directory / 'owned-regions.c'
    path.write_text(source)
    return path


HARNESS = r'''#include "xv_x86rt.h"
#include <stddef.h>
uint8_t *g_xram,*g_img_base; uint32_t *g_xpt;
unsigned xv_light_census_enabled;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void test_boot(void) {}
void xk_os_log(const char *f,...) {(void)f;}
char *getenv(const char *n) {(void)n;return 0;}
int atoi(const char *s) {(void)s;return 0;}
void __wrap_xv_preempt(xctx *c) {c->preempt=100;}
void *memcpy(void *d,const void *s,size_t n) {(void)s;(void)n;return d;}
void *memmove(void *d,const void *s,size_t n) {(void)s;(void)n;return d;}
void *memset(void *d,int s,size_t n) {(void)s;(void)n;return d;}
int snprintf(char *s,size_t n,const char *f,...) {(void)s;(void)n;(void)f;return 0;}
unsigned long strtoul(const char *s,char **e,int r) {(void)s;(void)e;(void)r;return 0;}
int sceClibPrintf(const char *f,...) {(void)f;return 0;}
/* Serial ARM lane only. Real production worker locks are checked separately. */
int xv_object_math_lock(void) {return 0;}
void xv_object_math_unlock(int *p) {(void)p;}
int xv_object_math_release_private(xctx *c,int *p,unsigned k,uint32_t o,unsigned n,uint32_t s,unsigned z)
{(void)c;(void)p;(void)k;(void)o;(void)n;(void)s;(void)z;return 0;}
void xv_object_math_report_check(void) {}
int xv_object_private_quaternion(xctx *c) {(void)c;return 0;}
int xv_object_census_scope_begin(void) {return 0;}
void xv_object_census_scope_end(int *p) {(void)p;}
void xv_object_basis_report(unsigned n) {(void)n;}
'''


def build(directory, source, retained, cc):
    harness = directory / 'arm-fixture.c'; harness.write_text(HARNESS)
    common = [cc, '-std=gnu11', '-O2', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon', '-fno-strict-aliasing',
              '-ffp-contract=off', '-I' + str(ROOT / 'recomp'), '-DXV_NATIVE_MODEL_PALETTE',
              '-DXV_NATIVE_MODEL_HIERARCHY', '-ffunction-sections', '-fdata-sections']
    commands = []; objects = []
    for i, path in enumerate((source, harness, ROOT / 'recomp/xv_x86rt.c')):
        obj = directory / f'fixture-{i}.o'; objects.append(str(obj))
        command = common + ['-c', str(path), '-o', str(obj)]
        subprocess.run(command, check=True); commands.append(command)
    production = [retained / 'kernel' / n for n in ('xk_hierarchy.o', 'xk_palette.o', 'xk_math.o')]
    names = [p + '_' + n for p in ('original', 'current', 'candidate') for n in ('hierarchy', 'palette')]
    command = common + objects + list(map(str, production)) + ['-nostdlib', '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,' +
        ','.join('--undefined=' + n for n in names + ['layout']), '-lgcc', '-o', str(directory / 'combined.elf')]
    subprocess.run(command, check=True); commands.append(command)
    return directory / 'combined.elf', commands, production


class Machine(base.Machine):
    def __init__(self, elf, h, p):
        self.h = h; self.p = p
        super().__init__(elf)
        self.unmodeled = {self.symbols[n] & ~1: n for n in ('snprintf', 'strtoul', 'sceClibPrintf') if n in self.symbols}

    def step(self, uc, address, size, user):
        if address in self.unmodeled:
            raise RuntimeError('Unexpected diagnostic import ' + self.unmodeled[address])
        name = self.imports.get(address)
        if name == 'getenv':
            self.instructions += 1
            key = bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0), 80)).split(b'\0')[0]
            value = {b'XV_NATIVE_MODEL_HIERARCHY': self.h, b'XV_NATIVE_MODEL_PALETTE': self.p,
                     b'XV_NATIVE_MATH': 1, b'XV_NATIVE_MATRIX_NEON': 0}.get(key)
            uc.reg_write(UC_ARM_REG_R0, 0 if value is None else base.ENV if value else base.ENV + 2)
            uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR))
            return
        if name == '__wrap_xv_preempt':
            ptr = uc.reg_read(UC_ARM_REG_R0)
            assert ptr == base.CTX
            self.observers.append((bytes(uc.mem_read(ptr, self.layout['size'])),
                                   hashlib.sha256(uc.mem_read(base.RAM, base.SIZE)).hexdigest(),
                                   hashlib.sha256(uc.mem_read(base.PT, 4 << 20)).hexdigest(), uc.reg_read(UC_ARM_REG_FPSCR)))
        super().step(uc, address, size, user)

    def run(self, function, fixture):
        memory, context, pages, fpscr = fixture; u = self.uc
        u.mem_write(base.RAM, memory); u.mem_write(base.CTX, context)
        u.mem_write(base.PT, bytes(4 << 20)); u.mem_write(base.PT, struct.pack('<' + 'I' * len(pages), *pages))
        u.mem_write(base.STACK, bytes(65536))
        u.reg_write(UC_ARM_REG_R0, base.CTX); u.reg_write(UC_ARM_REG_SP, base.STACK + 65024)
        u.reg_write(UC_ARM_REG_LR, base.END | 1); u.reg_write(UC_ARM_REG_FPSCR, fpscr)
        self.instructions = self.copies = self.copy_bytes = self.yields = 0; self.observers = []
        counters = ('batches', 'prepared', 'palette_batches', 'palette_matrices')
        before = {n: struct.unpack('<I', u.mem_read(self.symbols[n], 4))[0] for n in counters}
        u.emu_start(self.symbols[function] | 1, base.END, count=2000000)
        assert u.reg_read(UC_ARM_REG_PC) == base.END
        delta = {n: (struct.unpack('<I', u.mem_read(self.symbols[n], 4))[0] - before[n]) & 0xffffffff for n in counters}
        return dict(context=bytes(u.mem_read(base.CTX, self.layout['size'])), memory=bytes(u.mem_read(base.RAM, base.SIZE)),
                    pages=bytes(u.mem_read(base.PT, 4 << 20)), fpscr=u.reg_read(UC_ARM_REG_FPSCR),
                    observers=self.observers, observations=len(self.observers), instructions=self.instructions, yields=self.yields, **delta)


def physical_write(memory, pages, address, data):
    for i, value in enumerate(data): memory[pages[(address+i) >> 12] + ((address+i) & 4095)] = value


def palette_input(result, layout, pages, case):
    # Explicit fixture adapter, not a claim of an adjacent production call.
    context = bytearray(result['context']); memory = bytearray(result['memory'])
    sp = 0x82800; pose = 0x22fc0 if case.get('hierarchy_alias') else 0x52fc0; model = 0x12000
    if case.get('palette_alias'): sp = pose - 0xe4
    for reg, value in ((0, 0), (1, 0), (2, 0), (4, sp), (5, model), (6, 0), (7, pose)):
        struct.pack_into('<I', context, layout['r'] + reg*4, value)
    struct.pack_into('<i', context, layout['preempt'], 1 if case.get('palette_budget') else 1000)
    return bytes(memory), bytes(context), pages, result['fpscr']


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'retained-objects', 'output-dir'): ap.add_argument('--' + name, required=True, type=Path)
    ap.add_argument('--cc', default='arm-vita-eabi-gcc'); a = ap.parse_args(); a.output_dir.mkdir(parents=True, exist_ok=True)
    source = generate(a.xbe, a.manifest, a.output_dir)
    elf, commands, objects = build(a.output_dir, source, a.retained_objects, a.cc)
    machines = {(h,p): Machine(elf,h,p) for h,p in ((0,0),(1,0),(0,1),(1,1))}
    layout = machines[0,0].layout
    cases = [dict(name='admitted-chain', count=8), dict(name='admitted-tree', count=8, shape=1),
             dict(name='root', count=8, first=0), dict(name='short-tail', count=8, first=6),
             dict(name='two-nodes', count=2), dict(name='hierarchy-budget', count=8, variant=1),
             dict(name='palette-budget', count=8, palette_budget=True), dict(name='signed-zero', count=8, variant=2),
             dict(name='nan', count=8, variant=3), dict(name='subnormal', count=8, variant=4),
             dict(name='derived-overflow', count=8, variant=5), dict(name='split-pose', count=8, variant=6),
             dict(name='palette-exact-alias', count=8, palette_alias=True), dict(name='hierarchy-alias', count=8, hierarchy_alias=True)]
    rows = []
    for case in cases:
        for control in (0, 0x00400000, 0x00c0009f, 0x0300009f):
            sample = hierarchy.fixture(layout, case['count'], case.get('shape',0), case.get('first',1), control, case.get('variant',0))
            memory, context, pages, fpscr = sample; memory = bytearray(memory); context = bytearray(context)
            # Nonzero lazy backing fields and rotating x87 slots must survive
            # where the original continuation does not overwrite them.
            for offset, value in ((4,0x91a4b3c2),(8,0x12345678),(12,0xdeadbeef),
                                  (20,1),(24,1),(28,1),(32,1)):
                struct.pack_into('<I',context,layout['f_kind']+offset,value)
            struct.pack_into('<I',context,layout['fsp'],(len(rows)+3)%8)
            struct.pack_into('<H',context,layout['fcw'],0x27f if len(rows)&1 else 0x37f)
            for n in range(case['count']):
                for j in range(13):
                    value = 1.0 if j in (0,1,5,9) else (n*.03125 if j == 10 else 0.0)
                    physical_write(memory,pages,0x32fc0+n*156+0x68+j*4,struct.pack('<f',value))
            if case.get('hierarchy_alias'):
                physical_write(memory,pages,0x62800+0x24,struct.pack('<I',0x22fc0))
            sample = bytes(memory), bytes(context), pages, fpscr
            results = {}; reference = machines[0,0]
            # Existing native leaves are the authoritative baseline for exceptional
            # FP cases; prior leaf tests document original-lift NaN differences.
            for mode, machine in machines.items():
                h = machine.run('candidate_hierarchy',sample)
                p = machine.run('candidate_palette',palette_input(h,layout,pages,case))
                results[str(mode)] = (h,p)
            expected = results['(0, 0)']
            if case['name'] in ('admitted-chain','admitted-tree'):
                assert results['(1, 1)'][0]['batches'] == 1
                assert results['(1, 1)'][1]['palette_batches'] == 1
            for mode, stages in results.items():
                for stage,(actual,want) in enumerate(zip(stages,expected)):
                    for key in ('context','memory','pages','fpscr','observers'):
                        if actual[key] != want[key]:
                            folder = a.output_dir / ('mismatch-'+case['name']); folder.mkdir(exist_ok=True)
                            (folder/'description.json').write_text(json.dumps(dict(case=case,control=control,mode=mode,stage=stage,key=key),indent=2))
                            if isinstance(actual[key],bytes):
                                (folder/'actual.bin').write_bytes(actual[key]); (folder/'expected.bin').write_bytes(want[key])
                            raise AssertionError(str(folder))
            original_checked = case.get('variant',0) in (0,1,2) and not case.get('hierarchy_alias')
            if original_checked:
                h = reference.run('original_hierarchy',sample)
                p = reference.run('original_palette',palette_input(h,layout,pages,case))
                for actual,want in zip((h,p),expected):
                    for key in ('context','memory','pages','fpscr','observers'): assert actual[key] == want[key],(case,control,'original',key)
            rows.append(dict(case=case,control=control,original_checked=original_checked,runs={mode:[{k:v for k,v in stage.items() if k not in ('context','memory','pages','observers')} for stage in stages] for mode,stages in results.items()}))
            print('PASS',case['name'],hex(control),flush=True)
    report = dict(cases=len(rows),comparisons=len(rows)*3*2,context_bytes=layout['size'],arena_bytes=base.SIZE,commands=commands,
                  production_object_sha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in objects},
                  elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),rows=rows,
                  limitations=['Bounded synthetic dataflow adapter, not full model callers/gameplay','ARM serial guard imports modeled; separate host fixture exercises real worker locks','No FPS/cycle claim; matrix NEON runtime OFF'])
    (a.output_dir/'result.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__ == '__main__': main()
