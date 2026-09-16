#!/usr/bin/env python3
"""Synthetic off/on/off incremental build; no owned executable required."""
from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def run():
    with tempfile.TemporaryDirectory(prefix='xita-region-make-') as temp:
        d=Path(temp)
        def put(name, data=''):
            p=d/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_text(data)
            return p
        text=(ROOT/'Makefile').read_text().splitlines()
        # Use host C for synthetic empty units; all dependency/mode/archive rules
        # remain the actual checked-in Makefile rules.
        text=[('RECOMP_CFLAGS := -O0 -std=gnu11' if line.startswith('RECOMP_CFLAGS :=') else line) for line in text]
        put('Makefile','\n'.join(text)+'\n')
        put('games/halo_ce_3925/runtime.mk',(ROOT/'games/halo_ce_3925/runtime.mk').read_text())
        for name in ('tools/gen_native_bounds.py','tools/gen_native_clip.py','games/halo_ce_3925/clip_region.py','tools/gen_native_clip_region.py',
                     'games/halo_ce_3925/hooks.py','recompiler/xita_recomp.py',
                     'haloce/default.xbe','local/halo_ce_3925/game_manifest.json',
                     'recomp/xv_recomp_protos.h','recomp/xv_x86rt.h','recomp/xv_phase.h'):
            put(name)
        for name in ('quality','math','clip','bounds','flare','geometry','clip_region','clip_region_control'):
            put('recomp/kernel/xk_'+name+'.c','int synthetic_'+name+';\n')
        put('recomp/code_000.c', '''#ifdef XV_NATIVE_CLIP_REGION
extern void xv_math_clip_region(void);
void synthetic_guest(void) { xv_math_clip_region(); }
#else
void synthetic_guest(void) {}
#endif
''')
        prefix=d/'host'
        Path(str(prefix)+'-gcc-ar').symlink_to(shutil.which('gcc-ar'))
        env=dict(os.environ,VITASDK=str(d))
        command=['make','--no-print-directory','CC=cc','PREFIX='+str(prefix),
                 'PYTHON=false','build/recomp/code_000.o','build/recomp/libxita_game.a']
        last_time=None; hashes=[]
        for mode in (0,1,0,0):
            result=subprocess.run(command+['XV_NATIVE_CLIP_REGION='+str(mode)],cwd=d,env=env,
                                  check=True,capture_output=True,text=True)
            obj=d/'build/recomp/code_000.o'
            current=obj.stat().st_mtime_ns
            if last_time is not None:
                assert (current!=last_time) == (len(hashes)!=3), result.stdout
            last_time=current
            hashes.append(hashlib.sha256(obj.read_bytes()).hexdigest())
            undefined=subprocess.check_output(['nm','-u',str(obj)],text=True)
            assert ('xv_math_clip_region' in undefined)==bool(mode)
            members=subprocess.check_output(['ar','t',str(d/'build/recomp/libxita_game.a')],text=True).splitlines()
            for member in ('xk_clip_region.o','xk_clip_region_control.o'):
                assert (member in members)==bool(mode), (mode,members)
            assert (d/'build/recomp/clip-region.config').read_text()==str(mode)+'\n'
        assert hashes[0]==hashes[2]==hashes[3] and hashes[1]!=hashes[0]
        print('PASS off/on/off/unchanged: hook recompilation, exact restored object, archive membership')


if __name__=='__main__': run()
