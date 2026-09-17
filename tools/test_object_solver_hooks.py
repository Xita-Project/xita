#!/usr/bin/env python3
"""Qualify the collision solver closure using a user-owned supported image.

Writes translated reference code only to the supplied private output directory.
The production worker-pool fixture can link it with OBJECT_SOLVER_BODY.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from games.halo_ce_3925 import collision_solver
from games.halo_ce_3925.hooks import HaloHooks
from recompiler import xita_recomp as r
from recompiler.core.profile import load_profile

p = argparse.ArgumentParser(description=__doc__)
for name in ('xbe', 'manifest', 'symbols', 'stage', 'out'):
    p.add_argument('--'+name, required=True, type=Path)
args = p.parse_args()
img = r.Image(str(args.xbe), str(args.manifest))
profile = load_profile('halo_ce_3925'); profile.validate_image(img)
data = args.symbols.read_bytes(); profile.validate_symbols(data)
symbols = json.loads(data)
lift = r.DEFAULT_LIFT | set(profile.lift)
hle = {s['address']: s for s in symbols if s['kind'] == 'FUN' and s['name'] not in lift and
       (s['lib'] in r.HLE_LIBS or s['name'] in r.HLE_KEEP)}
hle.update(profile.overrides)
d = r.Discovery(img, hle, img.kernel_imports(), lambda *args: None)
for address in re.findall(r'^void f_([0-9A-F]{8})\(',
                         (args.stage/'recomp/xv_recomp_protos.h').read_text(), re.M):
    d.add_root(int(address, 16))
hooks = HaloHooks(img)
assert hooks.enabled and collision_solver.matches(img)
entry = '\n'.join(collision_solver.ENTRY)+'\n'
closure = {a for a, _, _ in collision_solver.SPANS} - {0x172BF0}
staged = {}
for unit in (args.stage/'recomp').glob('code_*.c'):
    text = unit.read_text()
    for match in re.finditer(r'^void f_([0-9A-F]{8})\([^\n]*\)\n\{\n.*?^\}', text, re.M | re.S):
        pc = int(match[1], 16)
        if pc in closure:
            assert pc not in staged
            staged[pc] = match[0]
assert set(staged) == closure
references = []
edges = {}
for pc in sorted(closure):
    fn = d.functions[pc]; d.lift_function(fn); d.split_blocks(fn)
    emitter = r.Emitter(img, d, hle, img.kernel_imports(), 'unused', 1, hooks=hooks)
    source = emitter.emit_function(fn)
    if pc == 0x170C10:
        assert source.count(entry) == 1
        instrumented = source
        source = source.replace(entry, '')
    else:
        assert 'xv_object_solver_' not in source
    assert source.rstrip() == staged[pc].rstrip(), ('staged solver drift', hex(pc))
    calls = set(int(a, 16) for a in re.findall(r'    f_([0-9A-F]{8})\(c\);', source))
    assert calls <= closure, (hex(pc), calls-closure)
    assert not any(s in source for s in ('XV_HLE_CALL', 'xv_lookup', 'xv_call('))
    edges[hex(pc)] = [hex(a) for a in sorted(calls)]
    references.append(source)
assert sum('xv_object_solver_begin' in s for s in references) == 0
# Every closure or caller change suppresses the entry hook; include last bytes
# to cover complete spans and the solver's emitted switch table.
read = img.bytes_at
for target, length, _ in collision_solver.SPANS:
    def altered(address, size):
        value = bytearray(read(address, size))
        if address == target: value[-1] ^= 1
        return bytes(value)
    img.bytes_at = altered
    assert not collision_solver.matches(img)
    assert not any('xv_object_solver_' in line for line in hooks.function_entry(0x170C10))
img.bytes_at = read
hooks.enabled = False
assert not hooks.function_entry(0x170C10)
args.out.mkdir(parents=True, exist_ok=True)
body = '#include "xv_x86rt.h"\n'
body += '\n'.join(f'void f_{a:08X}(xctx *);' for a in sorted(closure))+'\n'
body += '\n'.join(references)
body += '\n'+instrumented.replace('void f_00170C10(', 'void f_solver_hooked(', 1)
(args.out/'solver-reference-private.c').write_text(body)
(args.out/'solver-hooked-private.c').write_text(instrumented)
(args.out/'hooks.json').write_text(json.dumps(dict(
    functions=len(closure), signature_spans=len(collision_solver.SPANS),
    body_sha256=hashlib.sha256(body.encode()).hexdigest(), calls=edges,
    ordinary_body_unchanged=True, modified_closure_declines=True,
    enclosing_object_ordering_qualified=False), indent=2)+'\n')
print('PASS: 11-function closure, 12 complete signatures, unchanged original solver body, modified-image rejection')
