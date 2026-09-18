#!/usr/bin/env python3
"""Compare empty-object scan batches against independent owned-XBE lifts.

Generated reference code stays outside the repository. Requires the recompiler
dependencies, a matching owned executable and its manifest.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery


def reference_regions(body):
    out = '#include "xv_x86rt.h"\nextern unsigned reference_end;\n'
    # Each slice contains the original zero-identifier test, common increment
    # and back edge. A nonzero entry or loop exit is a test failure: the helper
    # must stop before either and cannot replace a scheduler handoff.
    regions = ((0x90190, 0x90196, 0x9020E, 0x90221),
               (0x90240, 0x90248, 0x9029F, 0x902A9),
               (0x902B6, 0x902BE, 0x90303, 0x9030D))
    for index, (start, live, tail, done) in enumerate(regions):
        label = lambda address: f'L_{address:08X}:'
        first = body[body.index(label(start)):body.index(label(live))]
        last = body[body.index(label(tail)):body.index(label(done))]
        first = first.replace(label(start), label(start) +
                              '\n    if (c->r[7] == reference_end) return;')
        out += f'void original_scan_{index}(xctx *restrict c)\n{{\n'
        out += first + last
        out += label(live) + '\n' + label(done) + '\n    __builtin_trap();\n}\n'
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', required=True)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    img = r.Image(args.xbe, args.manifest)
    hooks = HaloHooks(img)
    assert hooks.object_scan_enabled
    read = img.bytes_at
    def altered(address, size):
        data = bytearray(read(address, size))
        if (address, size) == (0x900E0, 0x239):
            data[-1] ^= 1
        return bytes(data)
    img.bytes_at = altered
    assert not HaloHooks(img).before_instruction(0x90190)
    img.bytes_at = read
    discovery = HaloDiscovery(img, {}, img.kernel_imports(), lambda *args: None)
    discovery.add_root(0x900E0)
    function = discovery.functions[0x900E0]
    discovery.lift_function(function)
    discovery.split_blocks(function)
    original = r.Emitter(img, discovery, {}, img.kernel_imports(), 'unused', 1,
                         hooks=NoGameHooks()).emit_function(function)
    candidate = r.Emitter(img, discovery, {}, img.kernel_imports(), 'unused', 1,
                          hooks=hooks).emit_function(function)
    assert candidate.count('xv_object_scan_empty(c,') == 6
    args.output_dir.mkdir(parents=True, exist_ok=True)
    reference = args.output_dir / 'original.c'
    reference.write_text(reference_regions(original))
    cc = os.environ.get('CC', 'cc')
    with tempfile.TemporaryDirectory(prefix='xita-object-scan-') as tmp:
        tmp = Path(tmp)
        results = []
        for label, text in (('original', original), ('candidate', candidate)):
            source = tmp / (label + '.c')
            source.write_text('#include "xv_x86rt.h"\n' + text)
            results.append(subprocess.check_output([cc, '-E', '-P',
                '-I' + str(ROOT / 'recomp'), str(source)]))
        assert results[0] == results[1], 'Compiled-out hook changes original body'
        binary = args.output_dir / 'test'
        subprocess.run([cc, '-O2', '-std=gnu11', '-fno-strict-aliasing',
            '-DXV_NATIVE_OBJECT_SCAN', '-ffunction-sections', '-fdata-sections',
            '-I' + str(ROOT / 'recomp'), *shlex.split(os.environ.get('TEST_CFLAGS', '')),
            str(ROOT / 'tools/tests/object_scan.c'), str(reference),
            str(ROOT / 'recomp/kernel/xk_object_scan.c'),
            str(ROOT / 'recomp/xv_x86rt.c'), '-Wl,--gc-sections,--wrap=xv_preempt',
            '-lm', '-o', str(binary)], check=True)
        for mode in ('on', 'off', 'default', 'environment'):
            subprocess.run([str(binary), mode], check=True)
    print('PASS: full-function identity, independent slices, compiled-out body')


if __name__ == '__main__':
    main()
