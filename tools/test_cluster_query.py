#!/usr/bin/env python3
"""Compare the typed traversal with an owned original numerical query prefix.

Generated game code remains in the caller's private output directory. This
does not prove guest scratch/context reconstruction or live snapshot lifetime.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks

IMAGE = '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
PCS = (0x56670, 0x52240, 0x51E90, 0x11840, 0xB77C0)


def generate(xbe, manifest, out, *, full=False):
    img = r.Image(str(xbe), str(manifest))
    assert hashlib.sha256(img.data).hexdigest() == IMAGE, 'unsupported image'
    disc = r.Discovery(img, {}, img.kernel_imports(), lambda *args: None)
    pcs = PCS + ((0xA9330,) if full else ())
    for pc in pcs:
        disc.add_root(pc)
        disc.lift_function(disc.functions[pc])
    # Small isolated discovery can retain overlapping tails when a newly found
    # block already begins inside an earlier one. Split at every known entry;
    # verify the original instruction bytes and unique PC coverage are retained.
    proof = {}
    for pc in pcs:
        fn = disc.functions[pc]
        before = {i.ip: img.bytes_at(i.ip, i.len) for b in fn.blocks.values() for i in b.insns}
        for b in fn.blocks.values():
            for index, ins in enumerate(b.insns):
                if index and ins.ip in fn.blocks:
                    b.insns = b.insns[:index]
                    b.end = ins.ip
                    b.succ = [ins.ip]
                    break
        instructions = [i for b in fn.blocks.values() for i in b.insns]
        after = {i.ip: img.bytes_at(i.ip, i.len) for i in instructions}
        assert before == after and len(after) == len(instructions)
        proof[hex(pc)] = dict(instructions=len(after),
            sha256=hashlib.sha256(b''.join(after[k] for k in sorted(after))).hexdigest())
    emit = r.Emitter(img, disc, {}, img.kernel_imports(), 'unused', 1, hooks=NoGameHooks())
    bodies = {pc: emit.emit_function(disc.functions[pc]) for pc in pcs}
    body = bodies[0x56670]
    bodies[0x56670] = body[:body.index('L_000566DE:')] + 'L_000566DE:\n    return;\n}\n'
    text = '#include "xv_x86rt.h"\n'
    text += ''.join(f'void f_{pc:08X}(xctx *);\n' for pc in pcs)
    text += ''.join(bodies.values())
    if full:
        tail = body[:body.index('L_00056670:')] + body[body.index('L_000566DE:'):]
        text += body.replace('f_00056670', 'f_query_whole')
        text += tail.replace('f_00056670', 'f_query_tail')
    assert not re.search(r'xv_call|xv_unimpl|xv_trap', text)
    for body in bodies.values():
        labels = re.findall(r'^L_([0-9A-F]{8}):', body, re.M)
        assert len(labels) == len(set(labels))
    (out / 'reference.c').write_text(text)
    axes = img.bytes_at(0x1eaf30, 24)
    (out / 'cluster_axes.h').write_text('static const unsigned char axes[24]={' +
                                      ','.join(map(str, axes)) + '};\n')
    (out / 'original-proof.json').write_text(json.dumps(proof, indent=2) + '\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--cases', type=int, default=1024)
    p.add_argument('--sanitize', action='store_true')
    a = p.parse_args()
    assert a.cases > 0
    a.out.mkdir(parents=True, exist_ok=True)
    generate(a.xbe, a.manifest, a.out)
    binary = a.out / ('oracle-asan' if a.sanitize else 'oracle')
    flags = ['-O2', '-g', '-std=gnu11', '-fno-strict-aliasing', '-ffp-contract=off',
             '-frounding-math', '-ffunction-sections', '-fdata-sections',
             '-I' + str(ROOT / 'recomp'), '-I' + str(a.out), '-DCASES=' + str(a.cases)]
    if a.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-no-pie']
    command = [os.environ.get('CC', 'cc'), *flags, str(a.out / 'reference.c'),
               str(ROOT / 'tools/tests/cluster_query.c'),
               str(ROOT / 'recomp/kernel/xk_cluster_query.c'),
               str(ROOT / 'recomp/xv_x86rt.c'),
               '-Wl,--gc-sections,--wrap=xv_preempt', '-lm', '-o', str(binary)]
    subprocess.run(command, check=True)
    (a.out / 'host-command.json').write_text(json.dumps(command, indent=2) + '\n')
    subprocess.run([str(binary)], check=True,
                   env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'), timeout=180)


if __name__ == '__main__':
    main()
