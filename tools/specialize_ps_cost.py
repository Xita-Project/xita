#!/usr/bin/env python3
"""Create a diagnostic gray-RGB variant of the qualified GREATER material.

This deliberately changes the image. It is not a playable optimization.
Only the final RGB expression changes; alpha/discard remain byte-for-byte intact.
"""
import argparse
from pathlib import Path
import re

RETURN = 'return saturate(float4(out_rgb, out_a));'
GRAY = 'return saturate(float4(float3(0.5, 0.5, 0.5), out_a));'
TAIL = ('float out_a = saturate(t0.a);\n'
        '    if (!(saturate(out_a) > xv_atest.x)) discard;\n'
        '    ' + RETURN + '\n}')


def specialize(source):
    code = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
    expected_tail = r'float\s+out_a\s*=\s*saturate\(t0\.a\);\s*if \(!\(saturate\(out_a\) > xv_atest\.x\)\) discard;\s*' + re.escape(RETURN) + r'\s*}\s*\Z'
    if source.count(RETURN) != 1 or not re.search(expected_tail, code):
        raise ValueError('Expected the exact qualified GREATER output block')
    if len(re.findall(r'\bdiscard\b', code)) != 1 or re.search(r'\bclip\s*\(', code):
        raise ValueError('Unexpected discard/clip behavior')
    if re.search(r':\s*(?:DEPTH\w*|SV_Depth\w*)\b', code, re.I):
        raise ValueError('Depth-writing shaders are outside this diagnostic')
    if code.count('float4 t3 = float4(0.0, 0.0, 0.0, 1.0);') != 1:
        raise ValueError('Expected qualified black-stage specialization')
    return source.replace(RETURN, GRAY)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.input.resolve() == args.output.resolve():
        parser.error('Use a distinct output; never overwrite the playable shader')
    args.output.write_text(specialize(args.input.read_text()))
