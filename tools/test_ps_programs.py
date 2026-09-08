#!/usr/bin/env python3
"""Compare runtime identities and generated programs against real captures."""
from dataclasses import asdict
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler.dx8_pixelshader_parse import decode_psdef
from recompiler import pixelshader_recomp_gen as gen
from tools.psdef_hash import canonicalize, canonical_hash, original_hash
from tools.ps_pipeline import vs_outputs


def main():
    definitions = {p.stem: p.read_bytes() for p in (ROOT / 'shaders/psdefs').glob('*.bin')}
    cases = []
    for raw, data in definitions.items():
        assert original_hash(data) == int(raw, 16), raw
        cases.extend([data, canonicalize(data)])
        n = min(data[0xd4], 8)
        changed = bytearray(data)
        for base in (0, 0x68, 0x88, 0xb4):
            for offset in range(base + n * 4, base + 32): changed[offset] ^= 0xa5
        assert canonical_hash(changed) == canonical_hash(data)
        cases.append(bytes(changed))
        changed = bytearray(data)
        changed[0x20] ^= 1  # active final combiner input must stay distinct
        assert canonical_hash(changed) != canonical_hash(data)
        cases.append(bytes(changed))
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp, 'key.c')
        source.write_text('#include <stdio.h>\n#include "runtime/xv_ps_key.h"\n'
                          'int main(void) { unsigned char d[240]; while(fread(d,1,240,stdin)==240) '
                          'printf("%08X\\n",xv_ps_program_key(d)); return 0; }\n')
        exe = Path(tmp, 'key')
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Werror', '-I', str(ROOT), str(source), '-o', str(exe)], check=True)
        values = subprocess.check_output([str(exe)], input=b''.join(cases)).decode().splitlines()
        assert [int(v, 16) for v in values] == [canonical_hash(d) for d in cases]

    outputs = vs_outputs()
    pairs = [line.split()[:2] for line in (ROOT / 'shaders/psdefs/pairs.txt').read_text().splitlines() if line.strip()]
    comparisons = 0
    for vs, raw in pairs:
        if raw not in definitions or int(vs, 16) not in outputs: continue
        gen.VARYINGS_AVAILABLE = outputs[int(vs, 16)][1]
        original = asdict(decode_psdef(definitions[raw], 0))
        canonical = asdict(decode_psdef(canonicalize(definitions[raw]), 0))
        cube_modes = sum(1 << t['index'] for t in original['textures'] if t['mode'] in gen.CUBE_MODES)
        for mask in range(16):
            if mask & ~cube_modes: continue
            gen.CUBE_2D_MASK = mask
            before = gen.generate(original, 'equivalence', False)[0]
            after = gen.generate(canonical, 'equivalence', False)[0]
            assert before == after, (vs, raw, mask)
            for stage in range(4):
                if mask & (1 << stage):
                    assert f'tex2D(tex{stage}, xv_cube_uv(' in after
                    assert f'samplerCUBE tex{stage}' not in after
            comparisons += 1
    gen.CUBE_2D_MASK = 0
    gen.VARYINGS_AVAILABLE = None
    print(f'PASS: {len(cases)} C/Python identity checks; {comparisons} source-equivalent texture variants')


if __name__ == '__main__': main()
