#!/usr/bin/env python3
"""Check sampled child hooks against an owned image and exact staged callback."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from games.halo_ce_3925.hooks import HaloHooks
from recompiler import xita_recomp as r
from recompiler.core.profile import load_profile

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--xbe',required=True,type=Path)
p.add_argument('--manifest',required=True,type=Path)
p.add_argument('--symbols',required=True,type=Path)
p.add_argument('--stage',required=True,type=Path)
p.add_argument('--out',required=True,type=Path)
args=p.parse_args()
img=r.Image(str(args.xbe),str(args.manifest))
profile=load_profile('halo_ce_3925');profile.validate_image(img)
data=args.symbols.read_bytes();profile.validate_symbols(data);symbols=json.loads(data)
lift=r.DEFAULT_LIFT|set(profile.lift)
hle={s['address']:s for s in symbols if s['kind']=='FUN' and s['name'] not in lift and
     (s['lib'] in r.HLE_LIBS or s['name'] in r.HLE_KEEP)}
hle.update(profile.overrides)
variables={s['name']:s['address'] for s in symbols if s['kind']=='VAR'}
variables.update(profile.variables)
d=r.Discovery(img,hle,img.kernel_imports(),lambda *args:None)
for address in re.findall(r'^void f_([0-9A-F]{8})\(',
                         (args.stage/'recomp/xv_recomp_protos.h').read_text(),re.M):
    d.add_root(int(address,16))
pc=0x4C980;d.lift_function(d.functions[pc]);d.split_blocks(d.functions[pc])
h=HaloHooks(img);assert h.enabled
old=HaloHooks(img);old.transform_body=lambda address,body:body
def emit(hooks):
    e=r.Emitter(img,d,hle,img.kernel_imports(),'unused',1,hooks=hooks)
    e.phase_targets=hooks.phase_targets();e.vars=variables
    return e.emit_function(d.functions[pc])
before,after=emit(old),emit(h)
pattern=r'^#if defined\(XV_EXPERIMENTAL_OBJECT_JOBS\) && defined\(XV_OBJECT_HOLD_PROFILE\)\n.*?^#else\n(.*?)^#endif\n'
stripped=re.sub(pattern,lambda m:m[1],after,flags=re.S|re.M)
assert stripped==before,'ordinary translated body changed'
assert before.count('XV_OBJECT_MATH_GUARD()')==after.count('XV_OBJECT_MATH_GUARD()')==1
calls=re.findall(r'    f_([0-9A-F]{8})\(c\);',before)
assert len(set(calls))==23
assert after.count('xv_object_hold_child_begin(xv_object_math_locked_)')==len(calls)
table=re.search(r'hold_children\[23\]=\{(.*?)\};',
                (ROOT/'recomp/kernel/xk_object_jobs.c').read_text(),re.S)[1]
ids=[int(n,16) for n in re.findall(r'0x([0-9A-F]+)',table)]
for call,child in re.findall(r'    f_([0-9A-F]{8})\(c\);\n      if \(hold_child_\) xv_object_hold_child_end\(hold_child_,(\d+)\);',after):
    assert int(call,16)==ids[int(child)]
staged=[]
for unit in (args.stage/'recomp').glob('code_*.c'):
    s=unit.read_text()
    m=re.search(r'^void f_0004C980\([^\n]*\)\n\{\n.*?^\}\n',s,re.M|re.S)
    if m:staged.append((unit,m[0]))
assert len(staged)==1
unit,source=staged[0]
# Earlier stage placed the guard before local mapping aliases. Preserve that
# exact placement and comment; the rest must match the current emitter.
guard='#ifdef XV_EXPERIMENTAL_OBJECT_JOBS\n    XV_OBJECT_MATH_GUARD(); /* shared guest transaction */\n#endif\n'
header='void f_0004C980(xctx *restrict c)\n{\n'
old_guard=guard.replace('shared guest transaction','shared list/datum transaction')
if source.startswith(header+old_guard):
    before=before.replace(guard,'').replace(header,header+old_guard,1)
    after=after.replace(guard,'').replace(header,header+old_guard,1)
assert source.rstrip()==before.rstrip(),'staged callback drift'
read=img.bytes_at
def changed(address,size):
    value=bytearray(read(address,size))
    if address==pc:value[-1]^=1
    return bytes(value)
img.bytes_at=changed
assert h.transform_body(pc,before)==before,'changed callback must decline'
h.enabled=False
assert h.transform_body(pc,before)==before,'unsupported executable must decline'
args.out.mkdir(parents=True,exist_ok=True)
(args.out/'callback-private.c').write_text(after.rstrip()+'\n')
(args.out/'hooks.json').write_text(json.dumps(dict(
    original_sha256=hashlib.sha256(source.encode()).hexdigest(),
    instrumented_sha256=hashlib.sha256((after.rstrip()+'\n').encode()).hexdigest(),
    direct_call_sites=len(calls),children=23,unit=unit.name,
    unchanged_without_diagnostic=True,modified_signature_declines=True),indent=2)+'\n')
print('PASS: exact staged callback, all direct calls/IDs, unchanged OFF body and guard, modified-image rejection')
