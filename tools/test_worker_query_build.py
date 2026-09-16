#!/usr/bin/env python3
"""Actual Makefile mode/archive rules on synthetic units; no owned assets."""
from pathlib import Path
import hashlib,os,shutil,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='worker-query-build-') as temp:
 d=Path(temp)
 def put(name,text=''):
  path=d/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text)
 lines=(ROOT/'Makefile').read_text().splitlines()
 put('Makefile','\n'.join('RECOMP_CFLAGS := -O0 -std=gnu11' if s.startswith('RECOMP_CFLAGS :=') else s for s in lines)+'\n')
 put('games/halo_ce_3925/runtime.mk',(ROOT/'games/halo_ce_3925/runtime.mk').read_text())
 for name in ('tools/gen_native_bounds.py','tools/gen_native_clip.py','tools/gen_worker_query.py',
              'games/halo_ce_3925/clip_region.py','games/halo_ce_3925/hooks.py','recompiler/xita_recomp.py',
              'haloce/default.xbe','local/halo_ce_3925/game_manifest.json','recomp/xv_recomp_protos.h',
              'recomp/xv_x86rt.h','recomp/xv_phase.h','recomp/kernel/xk_worker_query_generated.inc'):
  put(name)
 for name in ('quality','math','clip','bounds','flare','geometry','object_jobs','worker_query'):
  put('recomp/kernel/xk_'+name+'.c','int synthetic_'+name+';\n')
 put('recomp/code_000.c','#ifdef XV_WORKER_QUERY\nextern void xv_worker_query(void);\nvoid synthetic_guest(void){xv_worker_query();}\n#else\nvoid synthetic_guest(void){}\n#endif\n')
 prefix=d/'host';Path(str(prefix)+'-gcc-ar').symlink_to(shutil.which('gcc-ar'))
 command=['make','--no-print-directory','CC=cc','PREFIX='+str(prefix),'PYTHON=false',
          'XV_EXPERIMENTAL_OBJECT_JOBS=1','build/recomp/code_000.o','build/recomp/libxita_game.a']
 previous=None;hashes=[]
 for mode in (0,1,0,0):
  run=subprocess.run(command+['XV_WORKER_QUERY='+str(mode)],cwd=d,env=dict(os.environ,VITASDK=str(d)),capture_output=True,text=True)
  assert run.returncode==0,run.stdout+run.stderr
  obj=d/'build/recomp/code_000.o';mtime=obj.stat().st_mtime_ns
  if previous is not None:assert (mtime!=previous)==(len(hashes)!=3)
  previous=mtime;hashes.append(hashlib.sha256(obj.read_bytes()).hexdigest())
  undefined=subprocess.check_output(['nm','-u',str(obj)],text=True)
  assert ('xv_worker_query' in undefined)==bool(mode)
  members=subprocess.check_output(['ar','t',str(d/'build/recomp/libxita_game.a')],text=True).splitlines()
  assert ('xk_worker_query.o' in members)==bool(mode)
  assert (d/'build/recomp/worker-query.config').read_text()==str(mode)+'\n'
 assert hashes[0]==hashes[2]==hashes[3] and hashes[1]!=hashes[0]
 print('PASS: OFF/ON/OFF/unchanged hook objects and archive membership; exact restored OFF object')
