#!/usr/bin/env python3
"""Owned original oracle against production C adapter and actual object pool."""
from pathlib import Path
import argparse,json,os,re,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from tools.gen_worker_query import generate
p=argparse.ArgumentParser(description=__doc__)
for name in ('xbe','manifest','out'):p.add_argument('--'+name,type=Path,required=True)
p.add_argument('--sanitize',choices=('address','thread'));p.add_argument('--arm',action='store_true')
p.add_argument('--mode',action='append');p.add_argument('--reuse-generated',action='store_true')
p.add_argument('--adapter',type=Path,default=ROOT/'recomp/kernel/xk_worker_query.c');a=p.parse_args()
a.out.mkdir(parents=True,exist_ok=True)
if not a.reuse_generated:generate(a.xbe,a.manifest,ROOT/'recomp/kernel')
raw={int(k):v for k,v in json.loads((ROOT/'recomp/kernel/worker_query_original.json').read_text()).items()}
hooks=HaloHooks(r.Image(str(a.xbe),str(a.manifest)))
assert hooks.enabled
decl=''.join(f'void f_{pc:08X}(xctx *);\nvoid ref_{pc:08X}(xctx *);\n' for pc in raw)
text='#include "kernel/xk_object_jobs.h"\n'+decl
for pc,body in raw.items():
 text+=re.sub(r'\bf_([0-9A-F]{8})',r'ref_\1',body)
 # Actual production hook, after original guard, with original fallback/tail.
 entry='\n'.join(hooks.function_entry(pc))
 body=body.replace('{\n','{\n'+entry+'\n',1)
 text+=body
whole=raw[0x56670]
tail=whole[:whole.index('L_00056670:')]+whole[whole.index('L_000566DE:'):]
text+=re.sub(r'\bf_([0-9A-F]{8})',r'ref_\1',tail.replace('f_00056670','ref_commit'))
(a.out/'reference.c').write_text(text)
flags=['-O2','-g','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off','-frounding-math',
 '-ffunction-sections','-fdata-sections','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_WORKER_QUERY',
 '-I'+str(ROOT/'recomp'),'-I'+str(ROOT/'recomp/kernel')]
files=[a.out/'reference.c',ROOT/'tools/tests/worker_query.c',a.adapter,ROOT/'recomp/xv_x86rt.c']
if a.arm:
 cc=os.environ.get('ARM_CC','/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
 for name,path in [('adapter',files[2]),('reference',files[0]),('workers',ROOT/'recomp/kernel/xk_object_jobs.c')]:
  subprocess.run([cc,*flags,'-mthumb','-mcpu=cortex-a9','-mfpu=neon','-fstack-usage','-c',str(path),'-o',str(a.out/(name+'.o'))],check=True)
 subprocess.run(['/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-size',*[str(a.out/(n+'.o')) for n in ('adapter','reference','workers')]],check=True)
else:
 flags+=['-DXV_WORKER_QUERY_TEST']
 if a.sanitize:flags+=['-fsanitize='+a.sanitize+(',undefined' if a.sanitize=='address' else ''),'-fno-omit-frame-pointer','-no-pie']
 binary=a.out/'test'
 subprocess.run([os.environ.get('CC','cc'),*flags,*map(str,files),'-pthread','-Wl,--gc-sections','-lm','-o',str(binary)],check=True)
 for mode in (a.mode or ('normal','disabled','alias','mutation','parking','concurrent','budget')):
  run=subprocess.run([str(binary),mode],capture_output=True,text=True,timeout=120,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
  (a.out/(mode+'.log')).write_text(run.stdout+run.stderr)
  if mode=='budget':
   assert run.returncode!=0 and 'job instruction budget exceeded' in run.stderr,run
  else:
   assert run.returncode==0,run.stdout+run.stderr
  print('PASS:',mode,run.stdout.strip())
