#!/usr/bin/env python3
"""Run the real Makefile with real small host objects; no SDK/game build."""
import argparse,json,os,pathlib,shlex,shutil,subprocess,sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output-dir',type=pathlib.Path,required=True);a=p.parse_args()
OUT=a.output_dir.resolve();OUT.mkdir(parents=True,exist_ok=False)
def put(name,text='',generated=False):
    f=OUT/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(text)
    os.utime(f,(1_000_000_000+generated*10,)*2)
put('Makefile',(ROOT/'Makefile').read_text())
put('games/halo_ce_3925/runtime.mk',(ROOT/'games/halo_ce_3925/runtime.mk').read_text())
for n in ('recomp/xv_recomp_protos.h','recomp/xv_x86rt.h','recomp/xv_phase.h','recomp/xv_fn_table.c',
          'recomp/xv_stubs_default.c','recomp/code_000.c','runtime/xv_ui_gxm.c','shaders/halo_shaders.json',
          'recompiler/gen_layouts.py','recompiler/shader_recomp_gen.py','haloce/default.xbe','local/halo_ce_3925/game_manifest.json'):
    put(n)
put('runtime/xv_scene_census.h','/* real -MMD prerequisite */\n')
put('runtime/xv_scene_census_plan.h','/* planner prerequisite */\n')
put('runtime/main.c','#include "xv_scene_census.h"\n')
put('runtime/xv_d3d.c','#include "xv_scene_census.h"\n#include "xv_scene_census_plan.h"\n')
put('shaders/xv_layouts.h',generated=True)
recorder=OUT/'record.py'
recorder.write_text('''#!/usr/bin/env python3
import json,subprocess,sys
from pathlib import Path
args=sys.argv[1:]
with Path('commands.jsonl').open('a') as f:f.write(json.dumps(args)+'\\n')
if '-c' in args:
    flags=[v for v in args if v.startswith(('-I','-D')) or v in ('-MMD','-MP')]
    subprocess.run(['cc',*flags,'-c',args[args.index('-c')+1],'-o',args[args.index('-o')+1]],check=True)
else:
    assert args[0]=='rcs';subprocess.run(['ar',*args],check=True)
''');recorder.chmod(0o755)
shutil.copyfile(recorder,OUT/'fake-gcc-ar');(OUT/'fake-gcc-ar').chmod(0o755)
base=['make','--no-print-directory','RECOMP=1','VITASDK='+str(OUT),'CC='+shlex.join([sys.executable,str(recorder)]),'PREFIX='+str(OUT/'fake')]
targets=['build/runtime/main.o','build/runtime/xv_d3d.o','build/runtime/xv_ui_gxm.o','build/recomp/libxita_guest.a']
log=OUT/'commands.jsonl';rows=[];previous=None
modes=[(None,None),(0,0),(1,0),(1,56),(1,56),(1,0),(0,0),(0,56),(1,56),(1,4096)]
for feature,words in modes:
    log.write_text('');settings=[]
    if feature is not None:settings+=['XV_SCENE_CENSUS='+str(feature)]
    if words is not None:settings+=['XV_SCENE_CENSUS_NOTIFICATION_WORDS='+str(words)]
    command=base+settings+targets
    result=subprocess.run(command,cwd=OUT,capture_output=True,text=True)
    assert not result.returncode,result.stdout+result.stderr
    calls=[json.loads(s) for s in log.read_text().splitlines()]
    compiles={c[c.index('-c')+1]:c for c in calls if '-c' in c}
    effective=(feature or 0,(words or 0) if feature else 0)
    if previous is not None:
        wanted=set() if effective==previous else {'runtime/main.c'} if effective[0]==previous[0] else {'runtime/main.c','runtime/xv_d3d.c'}
        assert set(compiles)==wanted and len(calls)==len(compiles),calls
    for source,flags in compiles.items():
        assert ('-DXV_SCENE_CENSUS' in flags)==bool(feature and source in ('runtime/main.c','runtime/xv_d3d.c'))
        assert [v for v in flags if v.startswith('-DXV_SCENE_CENSUS_NOTIFICATION_WORDS=')]==([f'-DXV_SCENE_CENSUS_NOTIFICATION_WORDS={effective[1]}'] if feature and source=='runtime/main.c' else [])
    assert (OUT/'build/scene-census.config').read_text().strip()==str(effective[0])
    assert (OUT/'build/scene-census-words.config').read_text().strip()==str(effective[1])
    rows.append(dict(command=command,commands=calls));previous=effective
# Touch each real included interface and require precisely its owning objects.
for name,wanted in [('runtime/xv_scene_census_plan.h',{'runtime/xv_d3d.c'}),('runtime/xv_scene_census.h',{'runtime/main.c','runtime/xv_d3d.c'})]:
    (OUT/name).touch();log.write_text('')
    result=subprocess.run(base+['XV_SCENE_CENSUS=1','XV_SCENE_CENSUS_NOTIFICATION_WORDS=4096']+targets,cwd=OUT,capture_output=True,text=True)
    assert not result.returncode,result.stdout+result.stderr
    calls=[json.loads(s) for s in log.read_text().splitlines()]
    assert {c[c.index('-c')+1] for c in calls if '-c' in c}==wanted and len(calls)==len(wanted),calls
    rows.append(dict(touched=name,commands=calls))
for bad in ('','2','-1','0 1'):
    result=subprocess.run(base+['XV_SCENE_CENSUS='+bad,targets[0]],cwd=OUT,capture_output=True,text=True)
    assert result.returncode and 'XV_SCENE_CENSUS must be 0 or 1' in result.stderr
result=subprocess.run(base+['RECOMP=0','XV_SCENE_CENSUS=1',targets[0]],cwd=OUT,capture_output=True,text=True)
assert result.returncode and 'XV_SCENE_CENSUS requires RECOMP=1' in result.stderr
(OUT/'receipt.json').write_text(json.dumps(dict(result='PASS',steps=rows),indent=2)+'\n')
print('PASS 10 real Makefile transitions, capacity-only main rebuild, feature main/D3D rebuild, 2 header touches, no guest rebuild, invalid flag rejection')
