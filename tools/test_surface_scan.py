#!/usr/bin/env python3
"""Exact bounded surface-scan comparison against an independently lifted loop.

Owned image and generated original code stay in --out. This source-only probe
does not install a hook, establish source ownership or measure physical FPS.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks


def generate(xbe, manifest, out):
    if not __debug__:
        raise ValueError('Reference shape checks require Python assertions')
    image = r.Image(str(xbe), str(manifest))
    if hashlib.sha256(image.data).hexdigest() != '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae':
        raise ValueError('Unsupported image')
    spans = {0x54010: (619, '44fda8b07d8f13d81522d37702d0ee3e51e5a37194902b24ac7ab22e1bb95763'),
             0x54132: (15, '069824f3fb53ace725bd5642ca82282ffdc91776e31ecbc4913817ef2bf39ee9')}
    for address, (size, digest) in spans.items():
        if hashlib.sha256(image.bytes_at(address, size) or b'').hexdigest() != digest:
            raise ValueError('Unsupported surface loop')
    discovery = r.Discovery(image, {}, image.kernel_imports(), lambda *args: None)
    discovery.add_root(0x54010)
    function = discovery.functions[0x54010]
    discovery.lift_function(function); discovery.split_blocks(function)
    emitter = r.Emitter(image, discovery, {}, image.kernel_imports(), 'unused', 1, hooks=NoGameHooks())
    body = emitter.emit_function(function)
    region = body.split('L_00054132:\n', 1)[1].split('L_00054141:\n', 1)[0]
    assert region.count('X_PREEMPT()') == 1 and 'xv_call' not in region
    prefix = '#include "kernel/xk_surface_scan.h"\n#undef X_G\n'
    prefix += '#define X_G(a) ((void *)(arena+pages[(uint32_t)(a)>>12]+((uint32_t)(a)&4095)))\n'
    prefix += 'extern unsigned scan_entries,scan_batches;\n'
    for candidate in (False, True):
        name = 'candidate' if candidate else 'original'
        prefix += f'void {name}_surface_scan(xctx *restrict c,uint8_t *arena,const uint32_t *pages) {{\n'
        prefix += 'L_00054132:\n'
        if candidate:
            # EDX retains the original run start. Attempt at each eighth
            # backedge: a cost experiment, not a production ownership proof.
            prefix += '    if (c->preempt>8 && ((c->r[3]-c->r[2]) & 31u)==0 && c->r[3]!=c->r[2] && !(c->r[3]&3u)) { unsigned n=xv_surface_scan_batch(c,arena,pages); if(n){scan_entries+=n;scan_batches++;} }\n'
        prefix += region + '\nL_00054141: return;\n}\n'
    path = out / 'surface-reference.c'; path.write_text(prefix)
    (out / 'proof.json').write_text(json.dumps({'image_sha256': hashlib.sha256(image.data).hexdigest(),
        'region': '54132..54141', 'reference_sha256': hashlib.sha256(prefix.encode()).hexdigest()}, indent=2)+'\n')
    return path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'out'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--arm', action='store_true', help='Vita SDK instruction comparison; not FPS')
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=False)
    reference = generate(a.xbe, a.manifest, a.out)
    if a.arm:
        if a.sanitize:
            p.error('--sanitize is a host-only option')
        arm_run(a.out, reference)
        return
    command = [os.environ.get('CC', 'cc'), '-O2', '-g', '-std=gnu11', '-fno-strict-aliasing',
               '-I'+str(ROOT/'recomp'), *(['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie'] if a.sanitize else []),
               str(reference), str(ROOT/'tools/tests/surface_scan.c'),
               '-Wl,--wrap=xv_preempt', '-o', str(a.out/'test')]
    (a.out/'command.json').write_text(json.dumps(command, indent=2)+'\n')
    subprocess.run(command, check=True)
    subprocess.run([str(a.out/'test')], check=True, timeout=120,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=1'))


def arm_run(out, reference):
    from test_arm_cluster_runtime import RuntimeMachine, RAM, PT, SIZE
    cc = os.environ.get('ARM_CC', '/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    flags = ['-O2', '-g', '-std=gnu11', '-fno-strict-aliasing', '-mthumb',
             '-mcpu=cortex-a9', '-mfpu=neon', '-DTEST_ARM', '-I'+str(ROOT/'recomp'),
             '-ffunction-sections', '-fdata-sections']
    sources = [reference, ROOT/'tools/tests/surface_scan.c',
               ROOT/'tools/tests/cluster_runtime_arm_imports.c']
    commands, objects = [], []
    for i, source in enumerate(sources):
        obj = out/f'unit-{i}.o'
        command = [cc, *flags, '-c', str(source), '-o', str(obj)]
        subprocess.run(command, check=True); commands.append(command); objects.append(str(obj))
    names = ['arm_prepare', 'arm_original', 'arm_candidate', 'arm_context_ptr', 'layout',
             'scan_entries', 'scan_batches', 'scan_yields', 'g_img_base']
    elf = out/'surface.elf'
    command = [cc, *flags, *objects, '-nostdlib',
               '-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,'+
               ','.join('--undefined='+n for n in names), '-lc', '-lgcc', '-o', str(elf)]
    subprocess.run(command, check=True); commands.append(command)
    (out/'commands.json').write_text(json.dumps(commands, indent=2)+'\n')
    m = RuntimeMachine(elf)
    # Execute the fixture's yield handler, including its mutations. The generic
    # runner's ordinary instruction-budget stub would invalidate this test.
    m.imports = {a: n for a, n in m.imports.items() if n != '__wrap_xv_preempt'}
    specs = [(n, v, b) for n in (1, 3, 16, 128, 512)
             for v in (0, 64, 128, 1024, 2048, 10240) for b in (3, 10000)]
    specs += [(4096, v, 10000) for v in (0, 64)]
    rows = []
    for n, variant, budget in specs:
        m.call('arm_prepare', (n, variant, budget))
        before = bytes(m.uc.mem_read(RAM, SIZE)); pages = bytes(m.uc.mem_read(PT, 4 << 20))
        context = bytes(m.uc.mem_read(m.context, m.layout['size']))
        original = m.call('arm_original')
        expected = bytes(m.uc.mem_read(RAM, SIZE)); expected_pages = bytes(m.uc.mem_read(PT, 4 << 20))
        expected_context = bytes(m.uc.mem_read(m.context, m.layout['size']))
        expected_yields = bytes(m.uc.mem_read(m.symbols['scan_yields'], 4))
        m.uc.mem_write(RAM, before); m.uc.mem_write(PT, pages); m.uc.mem_write(m.context, context)
        for name in ('scan_entries', 'scan_batches', 'scan_yields'):
            m.uc.mem_write(m.symbols[name], bytes(4))
        candidate = m.call('arm_candidate')
        checks = {'context': bytes(m.uc.mem_read(m.context, m.layout['size'])) == expected_context,
                  'memory': bytes(m.uc.mem_read(RAM, SIZE)) == expected,
                  'pages': bytes(m.uc.mem_read(PT, 4 << 20)) == expected_pages,
                  'yields': bytes(m.uc.mem_read(m.symbols['scan_yields'], 4)) == expected_yields,
                  'fpscr': original['fpscr'] == candidate['fpscr']}
        row = {'n': n, 'variant': variant, 'budget': budget, 'original': original,
               'candidate': candidate, 'batched_entries': struct.unpack('<I', m.uc.mem_read(m.symbols['scan_entries'], 4))[0],
               'yields': struct.unpack('<I', expected_yields)[0]}
        if not all(checks.values()):
            (out/'failure.json').write_text(json.dumps(dict(row, checks=checks), indent=2)+'\n')
            raise AssertionError((n, variant, budget, checks))
        rows.append(row)
        print('PASS ARM surface scan', n, variant, budget, original['instructions'], candidate['instructions'], flush=True)
    (out/'result.json').write_text(json.dumps(rows, indent=2)+'\n')
    print('PASS', len(rows), 'exact ARM context/arena/page-table/FPSCR/yield comparisons')


if __name__ == '__main__':
    main()
