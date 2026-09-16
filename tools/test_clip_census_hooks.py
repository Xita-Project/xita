#!/usr/bin/env python3
"""Owned-image integration: preserve census hooks and real phase35/44 scopes."""
from pathlib import Path
import argparse,hashlib,json,re,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.clip_region import hook
from recompiler.core.profile import load_profile
p=argparse.ArgumentParser();p.add_argument('--out',type=Path)
p.add_argument('--symbols',type=Path);p.add_argument('--stage',type=Path);args=p.parse_args()
if args.stage and not args.symbols:p.error('--stage identity requires the exact --symbols input')
img=r.Image(str(ROOT/'haloce/default.xbe'),str(ROOT/'local/halo_ce_3925/game_manifest.json'))
hle={};variables={}
if args.symbols:
 profile=load_profile('halo_ce_3925');data=args.symbols.read_bytes()
 profile.validate_image(img);profile.validate_symbols(data);symbols=json.loads(data)
 lift=r.DEFAULT_LIFT|set(profile.lift)
 hle={s['address']:s for s in symbols if s['kind']=='FUN' and s['name'] not in lift and (s['lib'] in r.HLE_LIBS or s['name'] in r.HLE_KEEP)}
 hle.update(profile.overrides);variables={s['name']:s['address'] for s in symbols if s['kind']=='VAR'};variables.update(profile.variables)
h=HaloHooks(img);assert h.enabled and h.light_census_enabled and h.clip_region_enabled
baseline=HaloHooks(img);baseline.light_census_enabled=baseline.clip_region_enabled=False
phases=h.phase_targets();assert sorted(phases).index(0x92330)==35 and sorted(phases).index(0xB7F10)==44
assert 0x117B0 not in phases and 0xB71C0 not in phases
assert h.function_entry(0x117B0)==[]
assert h.function_entry(0xB71C0)==['    { extern int xv_math_polygon_clip(xctx *); if (xv_math_polygon_clip(c)) return; }']
pcs=(0x58440,0x58CD0,0x8D760,0x8D7A6,0x91D10,0x92230,0x92330,0xB7F10,0xB7F50,0xB8000)
d=r.Discovery(img,hle,img.kernel_imports(),lambda *args:None)
# Existing root boundaries matter: e.g.92230 tail-calls A92C0, whose
# shared transaction must not be inlined by a tiny isolated discovery.
if args.stage:
 for address in re.findall(r'^void f_([0-9A-F]{8})\(', (args.stage/'recomp/xv_recomp_protos.h').read_text(),re.M):
  d.add_root(int(address,16))
for pc in pcs:d.add_root(pc)
for pc in pcs:d.lift_function(d.functions[pc]);d.split_blocks(d.functions[pc])
new=r.Emitter(img,d,hle,img.kernel_imports(),'unused',1,hooks=h)
old=r.Emitter(img,d,hle,img.kernel_imports(),'unused',1,hooks=baseline)
new.phase_targets=old.phase_targets=phases
new.vars=old.vars=variables
bodies={};previous_bodies={}
for markers in (False,True):
 new.trace_funcs=old.trace_funcs=markers
 for pc in pcs:
  actual=new.emit_function(d.functions[pc]);previous=old.emit_function(d.functions[pc])
  stripped=re.sub(r'^#ifdef XV_LIGHT_QUERY_CENSUS\n.*?^#endif\n','',actual,flags=re.M|re.S)
  assert stripped==(hook(previous) if pc==0xB7F10 else previous),hex(pc)
  if pc==0x92330:
   assert actual.count('XV_PHASE_SCOPE(c, 35u);')==1
   assert 'X_PUSH32(0x925B0u);\n#ifdef XV_LIGHT_QUERY_CENSUS\n    XV_LIGHT_CENSUS_QUERY(c);\n#endif\n    f_00056670(c);' in actual
  if pc==0xB7F10:
   assert actual.count('XV_PHASE_SCOPE(c, 44u);')==1 and actual.count('xv_math_clip_region(c,')==1
  if not markers:bodies[pc]=actual;previous_bodies[pc]=previous
 assert sum(new.emit_function(d.functions[pc]).count('XV_LIGHT_CENSUS_END(c);') for pc in (0x8D760,0x8D7A6))==5
 assert sum(new.emit_function(d.functions[pc]).count('XV_LIGHT_CENSUS_REMOVE(c);') for pc in (0x8D760,0x8D7A6))==2
assert 'XV_LIGHT_CENSUS_SCOPE(c);' in bodies[0x8D760] and 'XV_LIGHT_CENSUS_SUFFIX(c);' in bodies[0x8D7A6]
for pc,reason in ((0x58440,'MAP_END'),(0x58CD0,'BSP_SWITCH'),(0x91D10,'LIST_RESET'),(0x92230,'LIGHT_DELETE')):
 assert 'XV_LIGHT_CENSUS_CANCEL(c,XV_LC_'+reason+');' in bodies[pc]
if args.stage:
 units={n:(args.stage/'recomp'/f'code_{n:03d}.c').read_text() for n in (10,13,14,16)}
 records=[]
 for pc in pcs:
  found=[]
  for n,text in units.items():
   match=re.search(r'^void f_'+f'{pc:08X}'+r'\([^\n]*\)\n\{\n.*?^\}\n',text,re.M|re.S)
   if match:found.append((n,match.group()))
  assert len(found)==1,hex(pc)
  unit,body=found[0]
  stripped=re.sub(r'^#ifdef XV_LIGHT_QUERY_CENSUS\n.*?^#endif\n','',body,flags=re.M|re.S)
  expected=hook(previous_bodies[pc]) if pc==0xB7F10 and 'xv_math_clip_region(c,' in body else previous_bodies[pc]
  # This stage inserted its outer timing statement before mapping locals.
  # Preserve that exact position; do not shift timing around the prologue.
  if pc in phases:
   phase=f'    XV_PHASE_SCOPE(c, {sorted(phases).index(pc)}u);\n'
   header=f'void f_{pc:08X}(xctx *restrict c)\n{{\n'
   if stripped.startswith(header+phase):
    expected=expected.replace(phase,'').replace(header,header+phase,1)
    bodies[pc]=bodies[pc].replace(phase,'').replace(header,header+phase,1)
  assert stripped.rstrip()==expected.rstrip(),(hex(pc),'stage body/HLE/phase mismatch; do not replace')
  records.append(dict(pc=hex(pc),unit=unit,sha256=hashlib.sha256(body.encode()).hexdigest()))
 if args.out:
  args.out.mkdir(parents=True,exist_ok=True)
  (args.out/'stage-identity.json').write_text(json.dumps(records,indent=2)+'\n')
 print('PASS exact read-only stage function identity, including production HLE routing and phases')
if args.out:
 args.out.mkdir(parents=True,exist_ok=True)
 for pc,body in bodies.items():(args.out/f'f_{pc:08X}.c').write_text(body.rstrip()+'\n')
print('PASS merged original-entry census, all5 exits/2 removes/query and lifetime hooks, real phase35/44, both marker modes and unchanged interior clip entries')
