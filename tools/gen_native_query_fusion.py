#!/usr/bin/env python3
"""Install the qualified separate-TU query transformation into owned outputs.

No game bytes are embedded here. Only code_028.c and query_fusion.c are written;
the five generic query bodies, including their interior entries, stay in their
existing units. Repeating generation validates and removes our exact previous
wrapper before applying the same transformation. Unexpected source drift fails
before any output is installed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import tempfile
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import prototype_collision_query as prototype

FEATURE = 'XV_NATIVE_QUERY_FUSION'
PREREQUISITES = ('XV_NATIVE_BSP_SPHERE', 'XV_NATIVE_COLLISION_VERTICES',
                 'XV_NATIVE_SEGMENT_SPHERE', 'XV_NATIVE_COLLISION_TRAVERSAL')
FRAMES = 32
GUARD = f'#ifndef {FEATURE}\n#define {FEATURE} 0\n#endif\n'
CHECKS = (f'#if {FEATURE} != 0 && {FEATURE} != 1\n'
          f'#error "{FEATURE} must be 0 or 1"\n#endif\n'
          f'#if {FEATURE} && (' + ' || '.join(f'!defined({p})' for p in PREREQUISITES) + ')\n'
          '#error "query fusion requires native BSP, vertices, segment sphere and typed traversal"\n#endif\n')
PREFIX = GUARD + CHECKS + f'#if {FEATURE}\n#include "xv_recomp_protos.h"\n' \
         'void nq_query_at_171f94(xctx *,unsigned);\nstatic void nq_collection_172c95(xctx *);\n#endif\n'
CALL = '    /* 00172C95  call 00171F10h */\n    X_PUSH32(0x172C9Au);\n    f_00171F10(c);'
SELECTED_CALL = CALL.replace('    f_00171F10(c);',
    f'#if {FEATURE}\n    nq_collection_172c95(c);\n#else\n    f_00171F10(c);\n#endif')


def digest(data):
    return hashlib.sha256(data.encode() if isinstance(data, str) else data).hexdigest()


def generic_caller(text):
    """Reverse only our exact previous transformation, never arbitrary edits."""
    if FEATURE not in text:
        if 'nq_collection_172c95' in text or 'nq_query_at_171f94' in text:
            raise ValueError('unexpected previous query-fusion transformation')
        return text
    if not text.startswith(PREFIX):
        raise ValueError('query-fusion caller prefix drift')
    text = text[len(PREFIX):]
    generic = prototype.extract_text(text, 'void f_00171F10(')
    if generic.count('f_00088110(c);') != 1:
        raise ValueError('generic collection query-call inventory drift')
    specialized = generic.replace('f_00088110(c);', 'nq_query_at_171f94(c,0x172c95u);')
    tail = f'\n#if {FEATURE}\nstatic void nq_collection_172c95(xctx *restrict c)\n{{' + specialized + '}\n#endif\n'
    if not text.endswith(tail) or text.count(SELECTED_CALL) != 1:
        raise ValueError('query-fusion clone/callsite drift')
    text = text[:-len(tail)].replace(SELECTED_CALL, CALL, 1)
    if FEATURE in text or 'nq_collection_172c95' in text or 'nq_query_at_171f94' in text:
        raise ValueError('unexpected extra fusion hook')
    return text


def replace_if_changed(path, text):
    data = text.encode()
    if path.exists() and path.read_bytes() == data:
        return False
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_bytes(data)
    temporary.replace(path)
    return True


def generate(xbe, manifest, recomp_dir, receipt):
    if not __debug__:
        raise RuntimeError('Refusing optimized Python: generation safety checks require assertions.')
    recomp_dir = Path(recomp_dir).resolve()
    # Validate even a previously generated caller against current owned-image
    # emission. The audited prototype also checks SHA, closure and shadow sinks.
    units = {i: (recomp_dir / f'code_{i:03d}.c').read_text() for i in (13, 16, 28)}
    original = {**units, 28: generic_caller(units[28])}
    with tempfile.TemporaryDirectory(prefix='.query-fusion-', dir=recomp_dir) as temporary:
        temp = Path(temporary)
        retained = temp / 'retained'
        retained.mkdir()
        for number, text in original.items():
            (retained / f'code_{number:03d}.c').write_text(text)
        out = temp / 'generated'
        out.mkdir()
        args = SimpleNamespace(xbe=Path(xbe), manifest=Path(manifest), out=out,
            frames=FRAMES, trace=False, entry_observers=False, inline_native=True,
            shadow_context=True, callers=True, full_caller=True, suite='cost',
            generic_fallback=False, disable_fusion=False, hold_profile_mode=1,
            candidate_traversal='typed', typed_default=1, negative_control=None,
            emit_only=True, retained_dir=retained, arm=False, cases=0,
            case_start=0, sanitize=False)
        prototype.generate(args)
        # The continuation machine does not save per-function lazy flag-cache
        # locals. In the qualified closure these are declarations/void casts
        # only; reject a future emitter that starts carrying cached flags.
        for address in prototype.ENTRIES:
            body = (out / f'original-{address:08X}-private.c').read_text()
            if any(len(re.findall(r'\b' + name + r'\b', body)) != 2
                   for name in ('fk_a', 'fk_b', 'fk_r')):
                raise ValueError(f'unreviewed local flag-cache use at {address:x}')
        generated = {}
        for name in ('code_028.c', 'query_fusion.c'):
            text = (out / 'integration' / name).read_text()
            text = text.replace('XV_QUERY_FUSION_PROTOTYPE', FEATURE)
            if not text.startswith(GUARD):
                raise ValueError('qualified generation guard drift')
            generated[name] = GUARD + CHECKS + text[len(GUARD):]
        if generic_caller(generated['code_028.c']) != original[28]:
            raise ValueError('caller transformation is not exactly reversible')
        oracle = json.loads((out / 'oracle.json').read_text())
        contract = json.loads((out / 'integration/contract.json').read_text())
    # Publication happens only after every input/output contract check passed.
    changed = [name for name, text in generated.items()
               if replace_if_changed(recomp_dir / name, text)]
    result = dict(feature=FEATURE, default=0, frames=FRAMES,
        owned_image_sha256=prototype.IMAGE, prerequisites=list(PREREQUISITES),
        source_body_sha256=oracle['body_sha256'], scope_inventory=oracle['scope_inventory'],
        input_sha256={f'code_{i:03d}.c': digest(t) for i, t in original.items()},
        output_sha256={name: digest(text) for name, text in generated.items()},
        generic_units_unchanged=['code_013.c', 'code_016.c'],
        caller_contract=contract, changed=changed)
    # A fresh receipt is also the build stamp. Write it after generated outputs,
    # including when their bytes were unchanged but an input was revalidated.
    receipt = Path(receipt)
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    if not __debug__:
        raise SystemExit('Refusing optimized Python: generation safety checks require assertions.')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--recomp-dir', type=Path, default=ROOT / 'recomp')
    parser.add_argument('--receipt', type=Path, required=True)
    args = parser.parse_args()
    result = generate(args.xbe, args.manifest, args.recomp_dir, args.receipt)
    print('query fusion: fixed 32 continuations; changed ' + (', '.join(result['changed']) or 'no source bytes'))


if __name__ == '__main__':
    main()
