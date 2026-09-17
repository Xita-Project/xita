#!/usr/bin/env python3
"""Check collision timing hooks and emit private, otherwise unchanged units."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from games.halo_ce_3925 import collision_solver, object_motion_profile as motion
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
symbols = json.loads(data); lift = r.DEFAULT_LIFT | set(profile.lift)
hle = {s['address']: s for s in symbols if s['kind'] == 'FUN' and s['name'] not in lift and
       (s['lib'] in r.HLE_LIBS or s['name'] in r.HLE_KEEP)}
hle.update(profile.overrides)
variables = {s['name']: s['address'] for s in symbols if s['kind'] == 'VAR'}
variables.update(profile.variables)
d = r.Discovery(img, hle, img.kernel_imports(), lambda *args: None)
for pc in re.findall(r'^void f_([0-9A-F]{8})\(', (args.stage/'recomp/xv_recomp_protos.h').read_text(), re.M):
    d.add_root(int(pc, 16))
hooks = HaloHooks(img); assert hooks.enabled
table = re.search(r'motion_sites\[MOTION_SITES\]=\{(.*?)\};',
                  (ROOT/'recomp/kernel/xk_object_jobs.c').read_text(), re.S)[1]
assert [int(s, 16) for s in re.findall(r'0x([0-9A-F]+)', table)] == [pc for pc, _, _ in motion.SPANS]
units = {}; results = []
for pc, size, digest in motion.SPANS:
    entry = '\n'.join(motion.entry(img, pc))+'\n'; assert 'motion_sample_' in entry
    fn = d.functions[pc]; d.lift_function(fn); d.split_blocks(fn)
    emitter = r.Emitter(img, d, hle, img.kernel_imports(), 'unused', 1, hooks=hooks)
    emitter.phase_targets = hooks.phase_targets(); emitter.vars = variables
    after = emitter.emit_function(fn)
    # This diagnostic build deliberately excludes the separate solver experiment.
    after = after.replace('\n'.join(collision_solver.ENTRY)+'\n', '')
    assert after.count(entry) == 1
    before = after.replace(entry, '')
    matches = []
    for unit in (args.stage/'recomp').glob('code_*.c'):
        source = units.get(unit.name, unit.read_text())
        m = re.search(r'^void f_'+f'{pc:08X}'+r'\([^\n]*\)\n\{\n.*?^\}\n', source, re.M | re.S)
        if m: matches.append((unit, source, m[0]))
    assert len(matches) == 1
    unit, source, staged = matches[0]
    guard = '#ifdef XV_EXPERIMENTAL_OBJECT_JOBS\n    XV_OBJECT_MATH_GUARD(); /* shared guest transaction */\n#endif\n'
    old_guard = guard.replace('shared guest transaction', 'shared list/datum transaction')
    header = f'void f_{pc:08X}(xctx *restrict c)\n{{\n'
    if staged.startswith(header+old_guard):
        before = before.replace(guard, '').replace(header, header+old_guard, 1)
        after = after.replace(guard, '').replace(header, header+old_guard, 1)
    original = staged.replace(entry, '')
    assert before.rstrip() == original.rstrip(), ('staged body drift', hex(pc))
    assert after.replace(entry, '').rstrip() == original.rstrip()
    units[unit.name] = source.replace(staged, after.rstrip()+'\n', 1)
    read = img.bytes_at
    def changed(address, length):
        value = bytearray(read(address, length))
        if address == pc and length == size: value[-1] ^= 1
        return bytes(value)
    img.bytes_at = changed
    assert not motion.entry(img, pc)
    assert not any('motion_sample_' in line for line in hooks.function_entry(pc))
    img.bytes_at = read
    results.append(dict(function=hex(pc), unit=unit.name, signature_bytes=size,
                        original_sha256=hashlib.sha256(staged.encode()).hexdigest(),
                        instrumented_sha256=hashlib.sha256(after.encode()).hexdigest()))
hooks.enabled = False
assert all(not hooks.function_entry(pc) for pc, _, _ in motion.SPANS)
args.out.mkdir(parents=True, exist_ok=True)
for name, source in units.items(): (args.out/name).write_text(source)
(args.out/'hooks.json').write_text(json.dumps(dict(
    functions=results, units=sorted(units), unchanged_without_diagnostic=True,
    unsupported_image_declines=True, modified_signature_declines=True,
    solver_experiment_included=False), indent=2)+'\n')
print(f'PASS: {len(motion.SPANS)} unchanged staged bodies, timing IDs, modified-image rejection; solver experiment excluded')
