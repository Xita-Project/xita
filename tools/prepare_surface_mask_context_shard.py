#!/usr/bin/env python3
"""Stage the qualified mask experiment in a private shard; never edit input.

The default and checked-address builds retain the original body. Enable the
experiment explicitly with XV_SURFACE_MASK_CONTEXT=1 in the isolated build.
"""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--shard', type=Path, required=True)
p.add_argument('--reference', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
source = a.shard.read_text()
reference = a.reference.read_text()
signature = 'void f_00053E90(xctx *restrict c)\n'
start = source.index(signature)
end = source.index('\nvoid f_', start + len(signature))
body = source[start + len(signature):end]
plain = body.replace('    XV_PHASE_SCOPE(c, 9u);\n', '')
expected = reference.split('void original(xctx *restrict c)\n', 1)[1].split('\n#define MASK_PUBLISH()', 1)[0].rstrip()
assert plain.rstrip() == expected, 'production body differs from qualified reference'
assert hashlib.sha256(plain.encode()).hexdigest() == '325e0b3eb548cb30a5f46f0d5922c1a635a9ad2c859a65cabfb1204b469b3b84', 'body pin differs'
macros = '\n#define MASK_PUBLISH()' + reference.split('\n#define MASK_PUBLISH()', 1)[1]
assert 'void candidate(xctx *restrict owner)\n' in macros
candidate = macros.replace('void candidate(xctx *restrict owner)\n', 'void f_00053E90(xctx *restrict owner)\n', 1)
if '    XV_PHASE_SCOPE(c, 9u);\n' in body:
    candidate = candidate.replace('    xctx state=*owner;', '    XV_PHASE_SCOPE(owner, 9u);\n    xctx state=*owner;', 1)
replacement = ('#if defined(XV_SURFACE_MASK_CONTEXT) && XV_SURFACE_MASK_CONTEXT && !defined(XV_CHECK_GUEST_ADDRESS)\n'
               + candidate.rstrip() + '\n#undef MASK_CONTEXT_PREEMPT\n#undef MASK_PUBLISH\n#else\n'
               + source[start:end] + '\n#endif\n')
result = source[:start] + replacement + source[end:]
assert result.replace(replacement, source[start:end], 1) == source
# Exclusive output creation protects retained build inputs.
with a.out.open('x') as f:
    f.write(result)
a.out.with_suffix('.audit.json').write_text(json.dumps({
    'source_sha256': hashlib.sha256(source.encode()).hexdigest(),
    'reference_sha256': hashlib.sha256(reference.encode()).hexdigest(),
    'output_sha256': hashlib.sha256(result.encode()).hexdigest(),
    'outside_target_restores_exactly': True,
    'default_enabled': False,
    'checked_address_fallback': True,
}, indent=2) + '\n')
