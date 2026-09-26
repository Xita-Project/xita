#!/usr/bin/env python3
"""Create opt-in axis/black variants from owned, generated material Cg.

Runtime requires exact constants, a captured all-black RGB upload in stage 3,
the matching 2D-cube material variant, and the matching alpha-test policy.
The default input is alpha-disabled; --keep-alpha preserves the generic test.
Compile outputs with the same compiler options as their original programs.
"""
import argparse
from pathlib import Path
import re


def specialize(source, keep_alpha=False):
    # Validate the audited generated alpha block separately, then leave that
    # entire block and its uniform byte-for-byte unchanged in the output.
    if keep_alpha:
        from specialize_ps_alpha import specialize as remove_alpha
        validated = remove_alpha(source)
    else:
        validated = source
    code = re.sub(r'//[^\n]*', '', validated)
    sample = 'float4 t3 = tex2D(tex3, xv_cube_uv(IN.texcoord3.xyz));'
    if code.count(sample) != 1 or re.search(r'\b(discard|clip)\b', code):
        raise ValueError('Expected an alpha-disabled 2D-cube material')
    if re.search(r'\bt3\.(?:[rgba]*a|[xyzw]*w)', code):
        raise ValueError('Stage-3 alpha dependency is outside the proof')
    if not re.search(r'float\s+out_a\s*=\s*saturate\(t0\.a\);', code):
        raise ValueError('Unexpected output-alpha dependency')
    for index in (0, 8, 5):
        uses = re.findall(r'psc\[' + str(index) + r'\](\.[a-z]+)?', code)
        if not uses or any(use != '.rgb' for use in uses):
            raise ValueError(f'Unexpected PSC{index} dependency')
    return (source.replace(sample, 'float4 t3 = float4(0.0, 0.0, 0.0, 1.0);')
            .replace('psc[0].rgb', 'float3(1.0, 0.0, 0.0)')
            .replace('psc[8].rgb', 'float3(0.0, 1.0, 0.0)')
            .replace('psc[5].rgb', 'float3(0.0, 0.0, 0.0)'))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('input', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--keep-alpha', action='store_true')
    p.add_argument("--greater", action="store_true", help="retain cutouts with captured GREATER policy")
    args = p.parse_args()
    result = specialize(args.input.read_text(), args.keep_alpha or args.greater)
    if args.greater:
        from specialize_ps_cutout import specialize as greater
        result = greater(result)
    args.output.write_text(result)
