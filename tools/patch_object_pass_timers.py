#!/usr/bin/env python3
"""Opt-in diagnostic spans for the owned 3925 object-update shard.

Require a reviewed function SHA256; fail before writing on any layout change.
Strip the tagged observer lines to recover the exact original function.
IDs F0900001/2/3 are synthetic elapsed spans, not guest functions. No per-object
clock reads are added. The regular-update span includes the worker join.
"""
import argparse
import hashlib
import re
from pathlib import Path

TAG = ' /* XV_OBJECT_PASS_PHASE */'

def transform(text, expected_sha256):
    match = re.search(r'^void f_000900E0\(xctx \*restrict c\)\n\{.*?^\}', text, re.M | re.S)
    if not match:
        raise ValueError('object update function missing')
    body = match[0]
    original = ''.join(line for line in body.splitlines(keepends=True) if TAG not in line)
    if hashlib.sha256(original.encode()).hexdigest() != expected_sha256:
        raise ValueError('reviewed object update SHA256 mismatch')
    def observer(kind, ident):
        return f'    {{ extern void xv_scene_phase_{kind}(uint32_t); xv_scene_phase_{kind}(0x{ident:X}u); }}{TAG}\n'
    boundaries = (
        ('000900E0  mov eax,ds:[2F8CA0h]', 1, observer('begin', 0xF0900001)),
        ('0009022D  mov ebp,ds:[2FC6ACh]', 2, observer('end', 0xF0900001)+observer('begin', 0xF0900002)),
        ('000902A9  mov esi,[ebp+34h]', 1, observer('end', 0xF0900002)+observer('begin', 0xF0900003)),
        ('00090314  jmp near ptr 0008ECA0h', 1, observer('end', 0xF0900003)),
    )
    result = original
    for instruction, count, code in boundaries:
        needle = f'    /* {instruction} */\n'
        if result.count(needle) != count:
            raise ValueError('object update boundary changed: '+instruction)
        result = result.replace(needle, code+needle)
    recovered = ''.join(line for line in result.splitlines(keepends=True) if TAG not in line)
    assert recovered == original
    return text[:match.start()]+result+text[match.end():]

if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('shard', type=Path)
    ap.add_argument('--expected-sha256', required=True)
    args=ap.parse_args()
    original=args.shard.read_text()
    result=transform(original,args.expected_sha256)
    args.shard.write_text(result)
    print('Installed once-per-tick activation, regular and lifecycle spans')
