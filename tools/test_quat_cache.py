#!/usr/bin/env python3
"""Compare optional quaternion reuse against the owned lift and current native path.

Generated references stay in a private output directory, never in the repository.
"""
import argparse
import hashlib
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


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe', required=True)
    p.add_argument('--manifest', required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    args = p.parse_args()
    img = r.Image(args.xbe, args.manifest)
    assert hashlib.sha256(img.bytes_at(0xB5F60, 291)).hexdigest() == '9f10d4414ec5fb6b39f5f20f6100791e0aec209d77f6c60599402dff6bdb5d78'
    discovery = r.Discovery(img, {}, img.kernel_imports(), lambda *a: None)
    discovery.add_root(0xB5F60)
    discovery.lift_function(discovery.functions[0xB5F60])
    discovery.split_blocks(discovery.functions[0xB5F60])
    emitter = r.Emitter(img, discovery, {}, img.kernel_imports(), 'unused', 1, hooks=NoGameHooks())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    reference = args.output_dir / 'original.c'
    reference.write_text('#include "xv_x86rt.h"\n' + emitter.emit_function(
        discovery.functions[0xB5F60]).replace('f_000B5F60', 'original_quaternion'))
    with tempfile.TemporaryDirectory(prefix='xita-quat-cache-') as tmp:
        tmp = Path(tmp)
        cc = os.environ.get('CC', 'cc')
        flags = ['-O2', '-std=gnu11', '-fno-strict-aliasing', '-ffp-contract=off',
                 '-ffunction-sections', '-fdata-sections', '-I' + str(ROOT / 'recomp'),
                 *shlex.split(os.environ.get('NATIVE_MATH_CFLAGS', ''))]
        baseline = tmp / 'baseline.o'
        subprocess.run([cc, *flags, '-Dxv_math_quaternion_matrix=current_quaternion',
                        '-Dxv_math_matrix_multiply=current_matrix',
                        '-Dxv_native_math_report=current_report', '-c',
                        str(ROOT / 'recomp/kernel/xk_math.c'), '-o', str(baseline)], check=True)
        binary = tmp / 'test'
        subprocess.run([cc, *flags, '-DXV_QUAT_CACHE',
                        str(ROOT / 'tools/tests/quat_cache.c'), str(reference), str(baseline),
                        str(ROOT / 'recomp/kernel/xk_math.c'),
                        str(ROOT / 'recomp/kernel/xk_quat_cache.c'),
                        str(ROOT / 'recomp/xv_x86rt.c'), '-Wl,--gc-sections', '-lm', '-o', str(binary)], check=True)
        for mode in ('enabled', 'unset', 'disabled', 'math-disabled'):
            subprocess.run([str(binary), mode], check=True)


if __name__ == '__main__':
    main()
