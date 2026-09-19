#!/usr/bin/env python3
"""Exact-production cross-model UV state, owner-lifetime and publication gate.

Requires owned retained sources/XBE, Unicorn/pyelftools and VitaSDK. Builds only
private ARM fixtures, never a game package or hardware deployment.
"""
from pathlib import Path
import argparse,subprocess,sys,json,hashlib
S=Path(__file__).resolve().parents[1]

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ['xbe','manifest','retained-build','out']:p.add_argument('--'+n,type=Path,required=True)
 a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
 args=[]
 for n in ['xbe','manifest','retained_build','out']:args+=['--'+n.replace('_','-'),str(getattr(a,n).resolve())]
 for tool,extra in [('test_model_uv.py',['--cross-model']),('test_model_uv_caller.py',[])]:
  with (out/(tool+'.log')).open('w') as f:subprocess.run([sys.executable,str(S/'tools'/tool),*args,*extra],stdout=f,stderr=subprocess.STDOUT,check=True)
 from model_uv_cross_model_checks import run as cross
 from model_uv_callbacks_checks import run as callbacks
 cross(out);callbacks(out)
 names=['results.json','caller-results.json','cross-results.json','callback-results.json']
 counts={n:json.loads((out/n).read_text())['comparisons']for n in names}
 sources=[S/'recomp/kernel/xk_model_uv.c',S/'recomp/kernel/xk_model_uv.h',S/'recomp/kernel/xk_owner_phase.c',out/'original.c',out/'uv.elf',out/'caller.elf']
 (out/'cross-production-receipt.json').write_text(json.dumps({'comparisons':counts,'total':sum(counts.values()),'flags':{'XV_MODEL_UV':1,'XV_MODEL_UV_CROSS_MODEL':1,'XV_OWNER_PHASE':1},'hashes':{str(n):hashlib.sha256(n.read_bytes()).hexdigest()for n in sources}},indent=2)+'\n')
 print('PASS',sum(counts.values()),'exact-production UV/lifecycle/publication comparisons')
if __name__=='__main__':
 if not __debug__:raise SystemExit('Run without Python -O')
 main()
