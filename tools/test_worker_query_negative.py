#!/usr/bin/env python3
"""Verify the actual oracle rejects four deliberately broken adapter copies."""
from pathlib import Path
import argparse,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
for name in ('xbe','manifest','out'):p.add_argument('--'+name,type=Path,required=True)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
source=(ROOT/'recomp/kernel/xk_worker_query.c').read_text()
mutations={
 'skip-validation':('q_validate(v,c,guard,lane);q_publish(v,c);','q_publish(v,c);','mutation'),
 'early-publication':('q_validate(v,c,guard,lane);q_publish(v,c);','q_publish(v,c);q_validate(v,c,guard,lane);','mutation'),
 'lost-dirty-stores':('for(unsigned i=off;i<off+n;i++)v->dirty[i>>6]|=UINT64_C(1)<<(i&63);','(void)off;','normal'),
 'lost-budget':('--c->preempt<=0','c->preempt<=0','normal'),
}
rows=[]
for name,(old,new,mode) in mutations.items():
 assert source.count(old)==1
 path=a.out/(name+'.c');path.write_text(source.replace(old,new))
 run=subprocess.run([sys.executable,str(ROOT/'tools/test_worker_query.py'),'--xbe',str(a.xbe),'--manifest',str(a.manifest),'--out',str(a.out/name),'--adapter',str(path),'--reuse-generated','--mode',mode],capture_output=True,text=True)
 (a.out/(name+'.log')).write_text(run.stdout+run.stderr)
 assert run.returncode!=0 and ('AssertionError' in run.stderr),run.stdout+run.stderr
 assert (a.out/name/'test').exists(),'compile failure is not a negative test'
 rows.append(dict(mutation=name,rejected=True))
 print('PASS: oracle rejects',name)
(a.out/'results.json').write_text(json.dumps(rows,indent=2))
