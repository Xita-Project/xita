#!/usr/bin/env python3
"""Compare native object basis preparation against an independent owned-XBE lift."""
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


def region(body, name):
    prefix = body[:body.index('L_0008DDF0:')].replace('f_0008DDF0', name)
    start = body.index('L_0008E166:')
    # The lifter can duplicate the next basic block on a fallthrough edge.
    # Stop at its first instruction, not its later canonical label.
    end = body.index('    /* 0008E293 ', start)
    return prefix + body[start:end] + 'L_0008E293:\n    return;\n}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', required=True)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    img = r.Image(args.xbe, args.manifest)
    hooks = HaloHooks(img)
    assert hooks.object_basis_enabled
    read = img.bytes_at
    def changed(address, size):
        data = bytearray(read(address, size))
        if (address, size) == (0x8E166, 301):
            data[-1] ^= 1
        return bytes(data)
    img.bytes_at = changed
    try:
        assert not HaloHooks(img).before_instruction(0x8E166)
    finally:
        img.bytes_at = read
    discovery = r.Discovery(img, {}, img.kernel_imports(), lambda *args: None)
    discovery.add_root(0x8DDF0)
    function = discovery.functions[0x8DDF0]
    discovery.lift_function(function)
    discovery.split_blocks(function)
    bodies = {}
    for label, hook in (('original', NoGameHooks()), ('candidate', hooks)):
        emitter = r.Emitter(img, discovery, {}, img.kernel_imports(), 'unused', 1, hooks=hook)
        bodies[label] = region(emitter.emit_function(function), label + '_basis')
    assert bodies['candidate'].count('if (xv_math_object_basis(c))') == 1
    with tempfile.TemporaryDirectory(prefix='xita-object-basis-') as directory:
        directory = Path(directory)
        cc = os.environ.get('CC', 'cc')
        include = '-I' + str(ROOT / 'recomp')
        preprocessed = []
        for label, body in bodies.items():
            path = directory / (label + '.c')
            path.write_text('#include "xv_x86rt.h"\n' + body.replace(label + '_basis', 'compare_basis'))
            preprocessed.append(subprocess.check_output([cc, '-E', '-P', include, str(path)]))
        assert preprocessed[0] == preprocessed[1]
        source = directory / 'original.c'
        source.write_text('#include "xv_x86rt.h"\n' + '\n'.join(bodies.values()))
        if args.output_dir:
            args.output_dir.mkdir(parents=True, exist_ok=True)
            (args.output_dir / 'original.c').write_text(source.read_text())
        binary = directory / 'test'
        subprocess.run([cc, '-O2', '-std=gnu11', '-fno-strict-aliasing', '-ffp-contract=off',
                        '-DXV_NATIVE_OBJECT_BASIS', '-ffunction-sections', '-fdata-sections', include,
                        *shlex.split(os.environ.get('NATIVE_MATH_CFLAGS', '')),
                        str(ROOT / 'tools/tests/object_basis.c'), str(source),
                        str(ROOT / 'recomp/kernel/xk_object_basis.c'), str(ROOT / 'recomp/xv_x86rt.c'),
                        '-Wl,--gc-sections', '-lm', '-o', str(binary)], check=True)
        for mode in ('enabled', 'unset', 'disabled', 'math-disabled'):
            subprocess.run([str(binary), mode], check=True)
    print('PASS: object-basis identity guard and default-off translation unchanged')


if __name__ == '__main__':
    main()
