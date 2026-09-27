#!/usr/bin/env python3
"""Instrument two native model hooks without changing their branch decisions.

Synthetic phase keys FFFF8E0F/FFFF8E16 name hierarchy/basis, not guest addresses.
Operate only on private generated code; refuse unexpected/partial patches.
"""
import argparse
from pathlib import Path
import re

MARK = '/* model native phase wrappers */'
SITES = [('xv_math_model_hierarchy', 'XV_NATIVE_MODEL_HIERARCHY', 'FFFF8E0F'),
         ('xv_math_object_basis', 'XV_NATIVE_OBJECT_BASIS', 'FFFF8E16')]


def wrappers():
    text = MARK + '\n'
    for name, macro, key in SITES:
        text += (f'#ifdef {macro}\n'
                 f'static int timed_{name}(xctx *c)\n{{\n'
                 f'    extern int {name}(xctx *);\n'
                 '    extern void xv_scene_phase_begin(uint32_t), xv_scene_phase_end(uint32_t);\n'
                 f'    xv_scene_phase_begin(0x{key}u);\n'
                 f'    int result = {name}(c);\n'
                 f'    xv_scene_phase_end(0x{key}u);\n'
                 '    return result;\n}\n#endif\n')
    return text


def patch(text):
    original = text
    prefix = wrappers()
    installed = MARK in text
    if installed:
        if text.count(prefix) != 1:
            raise ValueError('partial or modified model wrappers')
        text = text.replace(prefix, '', 1)
    matches = list(re.finditer(r'void f_0008DDF0\(xctx \*restrict c\)\n\{.*?\n\}', text, re.S))
    if len(matches) != 1:
        raise ValueError('expected exactly one model body')
    m = matches[0]
    body = m[0]
    for name, _, _ in SITES:
        old = f'{name}(c)'
        new = f'timed_{name}(c)'
        if installed:
            if body.count(new) != 1:
                raise ValueError('partial model call substitution')
            body = body.replace(new, old)
        elif 'timed_' + name in text:
            raise ValueError('unexpected partial timer')
        if body.count(old) != 1:
            raise ValueError('missing or ambiguous model hook')
        body = body.replace(old, new)
    out = text[:m.start()] + prefix + body + text[m.end():]
    if installed and out != original:
        raise ValueError('noncanonical installed patch')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('shard', type=Path)
    args = ap.parse_args()
    before = args.shard.read_text()
    after = patch(before)
    if after != before:
        args.shard.write_text(after)


if __name__ == '__main__':
    main()
