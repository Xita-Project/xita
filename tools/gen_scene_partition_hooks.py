#!/usr/bin/env python3
"""Validate the owned primary scene body and emit its passive partition only.

Inputs/outputs stay private. This never edits the supplied stage. Preserve its
unit prologues, prototype header and every unrelated function when installing
these bodies. No whole-game regeneration or instruction modification is needed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))


def generate(xbe,manifest,symbols,stage,out):
    if not __debug__: raise RuntimeError('Run without Python -O: identity checks require assertions')
    from recompiler import xita_recomp as r
    from recompiler.core.profile import load_profile
    from games.halo_ce_3925.hooks import HaloHooks
    img=r.Image(str(xbe),str(manifest));profile=load_profile('halo_ce_3925')
    data=symbols.read_bytes();profile.validate_image(img);profile.validate_symbols(data)
    parsed=json.loads(data);lift=r.DEFAULT_LIFT|set(profile.lift)
    hle={s['address']:s for s in parsed if s['kind']=='FUN' and s['name'] not in lift and (s['lib'] in r.HLE_LIBS or s['name'] in r.HLE_KEEP)}
    hle.update(profile.overrides)
    variables={s['name']:s['address'] for s in parsed if s['kind']=='VAR'};variables.update(profile.variables)
    hooks=HaloHooks(img);baseline=HaloHooks(img);baseline.scene_partition_enabled=False
    assert hooks.enabled and hooks.scene_partition_enabled
    d=r.Discovery(img,hle,img.kernel_imports(),lambda *args:None)
    for address in re.findall(r'^void f_([0-9A-F]{8})\(', (stage/'recomp/xv_recomp_protos.h').read_text(),re.M):d.add_root(int(address,16))
    pcs=(0x5D410,)
    for pc in pcs:d.add_root(pc);d.lift_function(d.functions[pc]);d.split_blocks(d.functions[pc])
    emitters=[]
    for h in (baseline,hooks):
        em=r.Emitter(img,d,hle,img.kernel_imports(),'unused',1,hooks=h)
        em.phase_targets=h.phase_targets();em.vars=variables;em.trace_funcs=False;emitters.append(em)
    saved={};records=[]
    for pc in pcs:
        found=[]
        for path in (stage/'recomp').glob('code_*.c'):
            text=path.read_text();m=re.search(r'^void f_'+f'{pc:08X}'+r'\([^\n]*\)\n\{\n.*?^\}\n',text,re.M|re.S)
            if m:found.append((path,m.group()))
        assert len(found)==1,(hex(pc),'ambiguous stage root')
        path,body=found[0];previous,actual=[em.emit_function(d.functions[pc]) for em in emitters]
        from games.halo_ce_3925.scene_partition import strip
        stripped=strip(actual)
        assert stripped==previous,(hex(pc),'observer alters instructions')
        assert actual.count('xv_scene_partition_begin(&xv_scene_partition_scope_, c)')==1
        # Retained production units put their existing phase statement first.
        phase=f'    XV_PHASE_SCOPE(c, {sorted(hooks.phase_targets()).index(pc)}u);\n'
        header=f'void f_{pc:08X}(xctx *restrict c)\n{{\n'
        if body.startswith(header+phase):
            previous=previous.replace(phase,'').replace(header,header+phase,1)
            actual=actual.replace(phase,'').replace(header,header+phase,1)
        assert previous.rstrip()==body.rstrip(),(hex(pc),'stage/HLE/entry/phase identity mismatch')
        assert actual.count(phase)==1
        saved[f'f_{pc:08X}.c']=actual.rstrip()+'\n'
        records.append(dict(address=f'{pc:08X}',unit=path.name,original_sha256=hashlib.sha256(body.encode()).hexdigest(),new_sha256=hashlib.sha256(saved[f'f_{pc:08X}.c'].encode()).hexdigest()))
    # Publish only after the exact reference body has validated.
    out.mkdir(parents=True,exist_ok=False)
    for name,body in saved.items():(out/name).write_text(body)
    receipt=dict(result='PASS',xbe_sha256=hashlib.sha256(img.data).hexdigest(),symbols_sha256=hashlib.sha256(data).hexdigest(),functions=records,scope='Primary 5D410 only; no original instruction, phase, HLE, interior root or shared prototype modification.')
    (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt


if __name__=='__main__':
    if not __debug__:raise SystemExit('Run without Python -O: identity checks require assertions')
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','manifest','symbols','stage','output-dir'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();print(json.dumps(generate(a.xbe,a.manifest,a.symbols,a.stage,a.output_dir),indent=2))
