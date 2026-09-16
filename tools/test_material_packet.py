#!/usr/bin/env python3
"""Generate exact guarded material spans and test the production D3D helper.

Requires the user's local supported XBE/manifest/symbols and iced-x86. No game
bytes or generated instruction bodies are committed. Optional --output-dir
retains the generated oracle and executables for ARM follow-up.
"""
from pathlib import Path
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.material_packets import SPANS


def strip_hooks(text):
    return re.sub(r'^#ifdef XV_MATERIAL_PACKET\n(?:    \{ extern int xv_material_packet[^\n]*|XV_MATERIAL_[0-9A-F]+:)\n#endif\n', '', text, flags=re.M)


def generate(directory):
    image = r.Image(str(ROOT / 'haloce/default.xbe'),
                    str(ROOT / 'local/halo_ce_3925/game_manifest.json'))
    symbols = json.loads((ROOT / 'local/halo_ce_3925/halo_symbols.json').read_text())
    hle = {s['address']: s for s in symbols if s['kind'] == 'FUN' and
           s['name'] not in r.DEFAULT_LIFT and (s['lib'] in r.HLE_LIBS or s['name'] in r.HLE_KEEP)}
    disc = r.Discovery(image, hle, image.kernel_imports(), lambda *args: None)
    disc.add_root(0x70110)
    disc.lift_function(disc.functions[0x70110])
    disc.split_blocks(disc.functions[0x70110])
    emitter = r.Emitter(image, disc, hle, image.kernel_imports(), 'unused', 1)
    original = emitter.emit_function(disc.functions[0x70110])
    emitter.hooks = HaloHooks(image)
    assert len(emitter.hooks.material_packets) == len(SPANS)
    hooked = emitter.emit_function(disc.functions[0x70110])
    stripped = strip_hooks(hooked)
    assert stripped == original, 'fallback instruction body changed'
    assert hooked.count('if (xv_material_packet(') == 5
    assert len(re.findall(r'^XV_MATERIAL_', hooked, re.M)) == 5
    # A changed complete image disables every island even when local bytes match.
    saved_data = image.data
    image.data = bytes([saved_data[0] ^ 1]) + saved_data[1:]
    assert not HaloHooks(image).material_packets
    image.data = saved_data
    read = image.bytes_at
    for start, end, _ in SPANS:
        for offset in (0, (end - start) // 2, end - start - 1):
            def changed(address, size, target=start, length=end-start, offset=offset):
                data = bytearray(read(address, size))
                if address == target and size == length:
                    data[offset] ^= 1
                return bytes(data)
            image.bytes_at = changed
            bad = HaloHooks(image)
            assert start not in bad.material_packets
            assert len(bad.material_packets) == 4
            emitter.hooks = bad
            emitted = emitter.emit_function(disc.functions[0x70110])
            assert f'goto XV_MATERIAL_{end:08X}' not in emitted
            assert f'XV_MATERIAL_{end:08X}:' not in emitted
            assert strip_hooks(emitted) == original
    image.bytes_at = read
    # No trace build may silently lose call observations. The helper declines
    # the runtime flags; emitting trace calls must leave all fallback calls.
    emitter.hooks = HaloHooks(image)
    emitter.trace = True
    trace = emitter.emit_function(disc.functions[0x70110])
    assert trace.count('xv_trace_call(') > 0 and trace.count('if (xv_material_packet(') == 5
    bodies = ['/* Generated privately from the supported local XBE. */']
    for i, (start, end, _) in enumerate(SPANS):
        a = original.index(f'    /* {start:08X} ')
        b = original.index(f'    /* {end:08X} ', a)
        body = original[a:b]
        assert 'X_PREEMPT' not in body
        bodies.append(f'void material_reference_{i}(xctx *c) {{\n{body}}}\n')
        a = hooked.index(f'    {{ extern int xv_material_packet(xctx *, unsigned); if (xv_material_packet(c, {i}u))')
        a = hooked.rfind('#ifdef XV_MATERIAL_PACKET\n', 0, a)
        b = hooked.index(f'    /* {end:08X} ', a)
        bodies.append(f'void material_hooked_{i}(xctx *c) {{\n{hooked[a:b]}    return;\n}}\n')
    (directory / 'material_packet_reference.inc').write_text('\n'.join(bodies))
    (directory / 'original_70110.c').write_text(original)
    (directory / 'hooked_70110.c').write_text(hooked)
    print('PASS: complete-image and 15 changed-span guards; exact unchanged fallback; five paired entry/resume hooks')


def run(directory):
    generate(directory)
    cc = os.environ.get('CC', 'cc')
    base = [cc, '-std=gnu11', '-O2', '-fno-strict-aliasing', '-Wall', '-Wextra',
            '-Wno-unused-parameter', '-Wno-unused-label', '-Wno-missing-field-initializers',
            '-Wno-misleading-indentation', '-Wno-stringop-truncation',
            '-ffunction-sections', '-fdata-sections', '-fsanitize=address,undefined',
            '-fno-omit-frame-pointer', '-I' + str(ROOT / 'recomp'), '-I' + str(directory),
            str(ROOT / 'tools/tests/material_packet.c'), '-Wl,--gc-sections', '-pthread', '-lm']
    env = {k: v for k, v in os.environ.items() if not k.startswith('XV_')}
    for name, flags in [('compiled-out', []), ('serial', []), ('workers', ['-DXV_EXPERIMENTAL_OBJECT_JOBS']),
                        ('missing', ['-DXV_EXPERIMENTAL_OBJECT_JOBS', '-DTEST_MISSING_WORKER_PREDICATE']),
                        ('traced', ['-DTEST_GUEST_TRACE=1'])]:
        exe = directory / ('material_packet_' + name)
        if name != 'compiled-out':
            flags = flags + ['-DXV_MATERIAL_PACKET']
        subprocess.run(base + flags + ['-o', str(exe)], check=True)
        if name in ('compiled-out', 'missing', 'traced'):
            subprocess.run([str(exe), 'decline'], env=env, check=True)
            continue
        for configured in (None, '0', '1'):
            test_env = dict(env)
            if configured is not None:
                test_env['XV_MATERIAL_PACKET'] = configured
            subprocess.run([str(exe)], env=test_env, check=True)
        for diagnostic in ('XV_D3D_HIST', 'XV_DS_CHECK', 'XV_LOG_RS', 'XV_WATCH_FN', 'XV_PROF'):
            subprocess.run([str(exe), 'decline'], env={**env, 'XV_MATERIAL_PACKET': '1', diagnostic: '1'}, check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--generate-only', action='store_true')
    args = parser.parse_args()
    if args.output_dir:
        args.output_dir.mkdir(parents=True, exist_ok=True)
        (generate if args.generate_only else run)(args.output_dir.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='xita-material-packet-') as tmp:
            (generate if args.generate_only else run)(Path(tmp))
