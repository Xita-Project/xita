#!/usr/bin/env python3
"""Exercise the real Make graph's scoped uv on/off transitions with tiny C fixtures."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=False)

    def put(name, text='', generated=False):
        path = out / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        os.utime(path, (1_000_000_000 + 10 * generated,) * 2)

    put('Makefile', (ROOT / 'Makefile').read_text())
    put('games/halo_ce_3925/runtime.mk', (ROOT / 'games/halo_ce_3925/runtime.mk').read_text())
    for name in ('recomp/xv_recomp_protos.h','recomp/xv_x86rt.h','recomp/xv_phase.h',
                 'recomp/xv_fn_table.c','recomp/xv_stubs_default.c','recomp/code_000.c',
                 'recomp/kernel/xk_owner_phase.h','recomp/kernel/xk_model_uv.h',
                 'tools/gen_native_clip.py','tools/gen_native_bounds.py',
                 'games/halo_ce_3925/hooks.py','games/halo_ce_3925/clip_region.py',
                 'games/halo_ce_3925/discovery.py','recompiler/xita_recomp.py',
                 'haloce/default.xbe','local/halo_ce_3925/game_manifest.json'):
        put(name)
    for name in ('xk_quality','xk_math','xk_clip','xk_bounds','xk_flare','xk_geometry','xk_owner_phase','xk_model_uv','xd3d'):
        put('recomp/kernel/'+name+'.c', generated=True)
    for number in (17,22):
        put(f'recomp/code_{number:03d}.c','/* XV_OWNER_PHASE_SCOPE: primary fixture */\n',generated=True)
    put('recomp/code_011.c', '/* XV_MODEL_UV_CALLS: primary fixture */\n', generated=True)
    put('recomp/code_015.c', '/* XV_MODEL_UV_SCOPE: primary fixture */\n', generated=True)
    recorder = out / 'record.py'
    recorder.write_text('''import json,subprocess,sys
    from pathlib import Path
    args=sys.argv[1:]
    with Path('commands.jsonl').open('a') as f:f.write(json.dumps(args)+'\\n')
    if '-c' in args:
     flags=[v for v in args if v.startswith(('-D','-I')) or v in ('-MMD','-MP')]
     subprocess.run(['cc',*flags,'-c',args[args.index('-c')+1],'-o',args[args.index('-o')+1]],check=True)
    else:
     assert args[0]=='rcs';subprocess.run(['ar',*args],check=True)
    '''.replace('\n    ', '\n'))
    wrapper = out / 'fake-gcc-ar'
    wrapper.write_text('#!/bin/sh\nexec '+shlex.join([sys.executable,str(recorder)])+' "$@"\n')
    wrapper.chmod(0o755)
    base = ['make','--no-print-directory','RECOMP=1','XV_OWNER_PHASE=1','VITASDK='+str(out),
            'CC='+shlex.join([sys.executable,str(recorder)]),'PREFIX='+str(out/'fake'),
            'XITA_SYS_SRCS=recomp/kernel/xd3d.c']
    targets = ['build/recomp/libxita_game.a','build/recomp/libxita_guest.a']
    rows=[]
    for mode in (0,1,1,0,0,1):
        (out/'commands.jsonl').write_text('')
        result=subprocess.run(base+[f'XV_MODEL_UV={mode}',*targets],cwd=out,text=True,capture_output=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        commands=[json.loads(s) for s in (out/'commands.jsonl').read_text().splitlines()]
        compiles={c[c.index('-c')+1]:c for c in commands if '-c' in c}
        if rows:
            if rows[-1]['mode']==mode:assert not commands,commands
            else:
                want={'recomp/code_011.c','recomp/code_015.c','recomp/kernel/xk_owner_phase.c'}
                if mode:want.add('recomp/kernel/xk_model_uv.c')
                assert set(compiles)==want,compiles
        for name,cmd in compiles.items():
            assert ('-DXV_MODEL_UV=1' in cmd)==bool(mode and name in {'recomp/code_011.c','recomp/code_015.c','recomp/kernel/xk_model_uv.c','recomp/kernel/xk_owner_phase.c'}),(name,cmd)
        if mode and 'recomp/kernel/xk_model_uv.c' in compiles:
            assert '-DXV_OWNER_PHASE' in compiles['recomp/kernel/xk_model_uv.c']
        members=subprocess.check_output(['ar','t',targets[0]],cwd=out,text=True).splitlines()
        assert ('xk_model_uv.o' in members)==bool(mode),members
        rows.append({'mode':mode,'commands':commands,'members':members})
    for bad in ('','2','-1','0 1'):
        result=subprocess.run(base+['XV_MODEL_UV='+bad,*targets],cwd=out,text=True,capture_output=True)
        assert result.returncode and 'XV_MODEL_UV must be 0 or 1' in result.stderr
    for extra in ('XV_OWNER_PHASE=0','RECOMP=0','GAME_PROFILE=halo2_5849'):
        result=subprocess.run(base+['XV_MODEL_UV=1',extra,*targets],cwd=out,text=True,capture_output=True)
        assert result.returncode
    (out/'recomp/code_011.c').write_text('/* missing hook */\n')
    result=subprocess.run(base+['XV_MODEL_UV=1',*targets],cwd=out,text=True,capture_output=True)
    assert result.returncode and 'regenerated primary A26B0 and 70110 UV hooks' in result.stderr
    (out/'receipt.json').write_text(json.dumps({'result':'PASS','builds':rows},indent=2)+'\n')
    print('PASS six real Make/archive transitions, scoped rebuilds, prerequisites and invalid configuration')


if __name__ == "__main__":
    if not __debug__:raise SystemExit("Run without Python -O")
    main()
