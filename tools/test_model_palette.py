#!/usr/bin/env python3
"""Compare the optional model batch against independent lifts of an owned XBE.

Generated game code stays in a temporary directory. No game bytes are fixtures.
Run in the recompiler Python environment with --xbe and --manifest.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks


def region(body, name):
    prefix = body[:body.index('L_000A26B0:')].replace('f_000A26B0', name)
    start = body.index('L_000A2781:')
    end = body.index('L_000A27C6:')
    return prefix + body[start:end] + 'L_000A27F2:\n    return;\n}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', required=True)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--output-dir', type=Path,
                        help='Private directory for generated reference code and evidence; never publish it')
    args = parser.parse_args()
    img = r.Image(args.xbe, args.manifest)
    hooks = HaloHooks(img)
    assert hooks.palette_enabled, 'Owned executable does not match the audited batch'
    read = img.bytes_at
    for address, size in ((0xA2781, 0x45), (0xB5B40, 339)):
        def changed(p, n):
            data = bytearray(read(p, n))
            if (p, n) == (address, size):
                data[-1] ^= 1
            return bytes(data)
        img.bytes_at = changed
        try:
            assert not HaloHooks(img).before_instruction(0xA2781)
        finally:
            img.bytes_at = read
    discovery = r.Discovery(img, {}, img.kernel_imports(), lambda *args: None)
    for address in (0xA26B0, 0xB5B40):
        discovery.add_root(address)
        discovery.lift_function(discovery.functions[address])
        discovery.split_blocks(discovery.functions[address])
    original = r.Emitter(img, discovery, {}, img.kernel_imports(), 'unused', 1,
                         hooks=NoGameHooks())
    candidate = r.Emitter(img, discovery, {}, img.kernel_imports(), 'unused', 1,
                          hooks=hooks)
    reference = region(original.emit_function(discovery.functions[0xA26B0]), 'original_palette')
    hooked = region(candidate.emit_function(discovery.functions[0xA26B0]), 'candidate_palette')
    assert hooked.count('if (xv_math_model_palette(c))') == 1
    with tempfile.TemporaryDirectory(prefix='xita-model-palette-') as directory:
        directory = Path(directory)
        cc = os.environ.get('CC', 'cc')
        include = '-I' + str(root / 'recomp')
        # With the experiment absent, the candidate is exactly the original
        # translated region after preprocessing (including scheduling calls).
        preprocessed = []
        for label, body in (('original', reference), ('candidate', hooked)):
            path = directory / (label + '.c')
            path.write_text('#include "xv_x86rt.h"\n' + body.replace(label + '_palette', 'compare_palette'))
            preprocessed.append(subprocess.check_output([cc, '-E', '-P', include, str(path)]))
        assert preprocessed[0] == preprocessed[1], 'Default-off candidate changed the translated body'
        source = directory / 'original.c'
        source.write_text('#include "xv_x86rt.h"\n' +
                          original.emit_function(discovery.functions[0xB5B40]).replace('f_000B5B40', 'original_matrix') +
                          candidate.emit_function(discovery.functions[0xB5B40]) +
                          reference.replace('f_000B5B40', 'original_matrix') +
                          reference.replace('original_palette', 'current_palette') + hooked)
        if args.output_dir:
            args.output_dir.mkdir(parents=True, exist_ok=True)
            (args.output_dir / 'original.c').write_text(source.read_text())
        binary = directory / 'test'
        subprocess.run([cc, '-O2', '-std=gnu11', '-fno-strict-aliasing', '-ffp-contract=off',
                        '-DXV_NATIVE_MODEL_PALETTE', '-ffunction-sections', '-fdata-sections', include,
                        *shlex.split(os.environ.get('NATIVE_MATH_CFLAGS', '')),
                        str(root / 'tools/tests/model_palette.c'), str(source),
                        str(root / 'recomp/kernel/xk_palette.c'), str(root / 'recomp/kernel/xk_math.c'),
                        str(root / 'recomp/xv_x86rt.c'),
                        '-Wl,--gc-sections,--wrap=xv_preempt', '-lm', '-o', str(binary)], check=True)
        for mode in ('enabled', 'unset', 'disabled', 'math-disabled'):
            subprocess.run([str(binary), mode], check=True)
    print('PASS: batch/callee identity guards and default-off translation unchanged')


if __name__ == '__main__':
    main()
