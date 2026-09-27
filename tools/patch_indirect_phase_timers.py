#!/usr/bin/env python3
"""Time a selected generated register-indirect tail call by resolved target.

Diagnostic stages only: keeps guest dispatch/order/state, but prevents host tail
call elimination. Validate every selected body before writing any shard.
"""
import argparse
from pathlib import Path
import re

MARK = '/* XV_INDIRECT_PHASE */'
CALL = re.compile(r'^([ \t]*)xv_call\(c, c->r\[([0-7])\]\); return;[ \t]*$', re.M)


def wrapped(indent, register):
    lines = [
        '{ ' + MARK,
        f'    uint32_t xv_indirect_target_ = c->r[{register}];',
        '    extern void xv_scene_phase_begin(uint32_t);',
        '    extern void xv_scene_phase_end(uint32_t);',
        '    xv_scene_phase_begin(xv_indirect_target_);',
        '    xv_call(c, xv_indirect_target_);',
        '    xv_scene_phase_end(xv_indirect_target_);',
        '} ' + MARK,
        'return;',
    ]
    return '\n'.join(indent + line for line in lines)


def patch_body(body):
    if MARK in body:
        # Accept only our exact emitted block, not a partial installation.
        match = re.search(r'^([ \t]*)\{ /\* XV_INDIRECT_PHASE \*/\n'
                          r'[ \t]*uint32_t xv_indirect_target_ = c->r\[([0-7])\];', body, re.M)
        if not match or body.count(MARK) != 2 or CALL.search(body):
            raise ValueError('Partial or ambiguous indirect timing block')
        if wrapped(*match.groups()) not in body:
            raise ValueError('Modified indirect timing block')
        return body
    matches = list(CALL.finditer(body))
    if len(matches) != 1 or body.count('xv_call(') != 1:
        raise ValueError('Expected exactly one register-indirect tail dispatch')
    return CALL.sub(lambda m: wrapped(*m.groups()), body)


def install(root, parents):
    pending = {}
    found = set()
    for path in sorted(root.glob('code_*.c')):
        source = path.read_text()
        for parent in parents:
            pattern = re.compile(r'^void f_' + parent + r'\(xctx \*restrict c\)\n\{\n', re.M)
            match = pattern.search(source)
            if not match:
                continue
            if parent in found or len(pattern.findall(source)) != 1:
                raise ValueError('Duplicate parent ' + parent)
            found.add(parent)
            following = re.search(r'^\S[^\n]*\([^\n]*\)\n\{', source[match.end():], re.M)
            end = match.end() + following.start() if following else len(source)
            source = source[:match.end()] + patch_body(source[match.end():end]) + source[end:]
        if source != path.read_text():
            pending[path] = source
    if found != set(parents):
        raise ValueError('Missing parents: ' + ','.join(sorted(set(parents) - found)))
    for path, source in pending.items():
        path.write_text(source)
    return len(pending)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('--parents', required=True)
    args = parser.parse_args()
    parents = [f'{int(value, 16):08X}' for value in args.parents.split(',')]
    if len(parents) != len(set(parents)):
        parser.error('Duplicate parent selection')
    print('Changed shards:', install(args.root, parents))
