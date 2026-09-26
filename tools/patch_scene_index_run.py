#!/usr/bin/env python3
"""Install the opt-in, private-stack scene index hook into an owned CE shard.

Only private generated code is modified. The retained loop must match its
previously verified hash, and remains the fallback in every mode.
"""
import argparse
import hashlib
from pathlib import Path

HASH = 'a695e16fa083c16448f0492c4f3de2ac78c640313483e758b418e15bea200d4e'
INCLUDE = '#include "kernel/xk_scene_index_hook.h"\n'
BEGIN = '    xv_scene_index_scope index_scope; xv_scene_index_begin(&index_scope, c);\n'
STEP = '    xv_scene_index_step(&index_scope, c, xram_, xpt_);\n'
END = '    xv_scene_index_end_run(&index_scope);\n'


def instrument(source):
    start = source.index('void f_00054010(xctx *restrict c)\n{\n')
    end = source.find('\nvoid f_', start + 1)
    if end < 0:
        end = len(source)
    body = source[start:end]
    if BEGIN in body or INCLUDE in source:
        if (source.count(INCLUDE), body.count(BEGIN), body.count(STEP), body.count(END)) != (1, 1, 1, 1):
            raise ValueError('partial scene index hook')
        clean = source.replace(INCLUDE, '', 1).replace(BEGIN, '', 1).replace(STEP, '', 1).replace(END, '', 1)
        if instrument(clean) != source:
            raise ValueError('modified scene index hook')
        return source
    if body.count('L_00054132:\n') != 1 or body.count('L_00054141:\n') != 1:
        raise ValueError('unexpected loop labels')
    loop = body.split('L_00054132:\n', 1)[1].split('L_00054141:\n', 1)[0]
    if hashlib.sha256(loop.encode()).hexdigest() != HASH:
        raise ValueError('retained loop differs from verified reference')
    body = body.replace('{\n', '{\n' + BEGIN, 1)
    body = body.replace('L_00054132:\n', 'L_00054132:\n' + STEP)
    body = body.replace('L_00054141:\n', 'L_00054141:\n' + END)
    return INCLUDE + source[:start] + body + source[end:]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('shard', type=Path)
    args = ap.parse_args()
    old = args.shard.read_text()
    new = instrument(old)
    if new != old:
        args.shard.write_text(new)
    print('54010: opt-in scene index hook ' + ('installed' if new != old else 'already present'))


if __name__ == '__main__':
    main()
