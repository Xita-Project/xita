#!/usr/bin/env python3
"""Compare the native collision object walk with independent owned-XBE lifts.

Runs the complete original 0x171F10 and the hooked emission from identical
synthetic worlds. Both share the unmodified translated 0x1716F0 and 0x1D130;
their remaining callees are recording stubs. Generated reference code stays in
--output-dir, outside the repository. Requires the recompiler dependencies, a
matching owned executable and its manifest.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks

SPANS = ((0x171F10, 672), (0x1716F0, 605))
LIFTED = (0x171F10, 0x1716F0, 0x1D130)
STUBS = (0x88110, 0x868F0, 0x487E0, 0x855F0, 0x81900, 0x81770, 0x172DE0, 0x172F40)


def lift(img, entry, hooks):
    discovery = r.Discovery(img, {}, img.kernel_imports(), lambda *args: None)
    discovery.add_root(entry)
    function = discovery.functions[entry]
    discovery.lift_function(function)
    discovery.split_blocks(function)
    return r.Emitter(img, discovery, {}, img.kernel_imports(), 'unused', 1,
                     hooks=hooks).emit_function(function)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', required=True)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=3000)
    parser.add_argument('--sanitize', action='store_true', help='build with ASan and UBSan')
    parser.add_argument('--helper', type=Path, default=ROOT / 'recomp/kernel/xk_object_collect.c',
                        help='helper source; another revision serves as a negative control')
    parser.add_argument('--modes', nargs='+', default=['on', 'off', 'default', 'environment'])
    parser.add_argument('--regression', type=int, choices=(1, 2),
                        help='run only one review regression kind (negative controls)')
    args = parser.parse_args()
    img = r.Image(args.xbe, args.manifest)
    hooks = HaloHooks(img)
    assert hooks.object_collect_enabled
    read = img.bytes_at
    for span in SPANS:
        def altered(address, size, span=span):
            data = bytearray(read(address, size))
            if (address, size) == span:
                data[-1] ^= 1
            return bytes(data)
        img.bytes_at = altered
        assert not HaloHooks(img).before_instruction(0x172034), 'signature drift retained hook'
    img.bytes_at = read

    bodies = {entry: lift(img, entry, NoGameHooks()) for entry in LIFTED}
    candidate = lift(img, 0x171F10, hooks)
    assert candidate.count('xv_object_collect_refs(c)') == 1
    assert candidate.count('L_00172163:') == 1 and candidate.count('L_00172034:') == 1
    callees = set(re.findall(r'f_([0-9A-F]{8})\(c\);', ''.join(bodies.values())))
    assert callees == {f'{a:08X}' for a in STUBS + (0x1716F0, 0x1D130)}, sorted(callees)
    assert not re.search(r'xv_call|xv_unimpl', ''.join(bodies.values()) + candidate)

    args.output_dir.mkdir(parents=True, exist_ok=True)
    reference = args.output_dir / 'original.c'
    header = '#include "xv_x86rt.h"\n' + ''.join(
        f'void f_{a:08X}(xctx *);\n' for a in STUBS + (0x1716F0, 0x1D130))
    reference.write_text(header + bodies[0x1D130] + bodies[0x1716F0] +
        bodies[0x171F10].replace('void f_00171F10(', 'void reference_00171F10(', 1) +
        candidate.replace('void f_00171F10(', 'void candidate_00171F10(', 1))
    cc = os.environ.get('CC', 'cc')
    with tempfile.TemporaryDirectory(prefix='xita-object-collect-') as tmp:
        tmp = Path(tmp)
        results = []
        for label, text in (('original', bodies[0x171F10]), ('candidate', candidate)):
            source = tmp / (label + '.c')
            source.write_text('#include "xv_x86rt.h"\n' + text)
            results.append(subprocess.check_output([cc, '-E', '-P',
                '-I' + str(ROOT / 'recomp'), str(source)]))
        assert results[0] == results[1], 'Compiled-out hook changes original body'
    binary = args.output_dir / 'test'
    sanitize = ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer'] if args.sanitize else []
    subprocess.run([cc, '-O2', '-std=gnu11', '-fno-strict-aliasing', '-ffp-contract=off',
        '-DXV_NATIVE_OBJECT_COLLECT', '-ffunction-sections', '-fdata-sections', *sanitize,
        '-I' + str(ROOT / 'recomp'), '-I' + str(ROOT / 'recomp/kernel'),
        *shlex.split(os.environ.get('TEST_CFLAGS', '')),
        str(ROOT / 'tools/tests/object_collect.c'), str(reference),
        str(args.helper), str(ROOT / 'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections,--wrap=xv_preempt,--wrap=xv_trap', '-lm', '-o', str(binary)],
        check=True)
    environment = dict(os.environ)
    environment.pop('OBJECT_COLLECT_REGRESSION', None)
    if args.regression:
        environment['OBJECT_COLLECT_REGRESSION'] = str(args.regression)
    sources = [args.helper, ROOT / 'tools/tests/object_collect.c', Path(__file__).resolve(),
               ROOT / 'games/halo_ce_3925/hooks.py', ROOT / 'recomp/xv_x86rt.c', ROOT / 'recomp/xv_x86rt.h']
    receipt = dict(argv=sys.argv, sanitize=args.sanitize, regression=args.regression,
                   sources={str(p): hashlib.sha256(Path(p).read_bytes()).hexdigest() for p in sources},
                   modes={})
    try:
        for mode in args.modes:
            run = subprocess.run([str(binary), mode, str(args.cases)], env=environment,
                                 capture_output=True, text=True)
            receipt['modes'][mode] = dict(returncode=run.returncode, stdout=run.stdout,
                                          stderr=run.stderr[-4000:])
            sys.stdout.write(run.stdout)
            sys.stderr.write(run.stderr[-4000:])
            if run.returncode:
                raise SystemExit(f'FAIL mode {mode}: exit {run.returncode}')
    finally:
        (args.output_dir / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS: signature guards, compiled-out body, full-context walk comparisons')


if __name__ == '__main__':
    main()
