#!/usr/bin/env python3
"""Prepare two guarded data boundaries in the pinned Halo subcluster caller."""
import argparse
import hashlib
import json
from pathlib import Path
from audit_subcluster_contract import audit
ROOT=Path(__file__).resolve().parents[1]
FLAG='XV_TYPED_SUBCLUSTER'
PIN='75406ef2b4e74a0ef9431342e19f0853a8650faff04edf008a4ab984eafdfbbe'

def transform(body):
    if hashlib.sha256(body.encode()).hexdigest()!=PIN:raise ValueError('subcluster caller changed')
    if body.count('    f_0005C300(c);')!=1 or body.count('L_00052ED7:')!=1:raise ValueError('call/loop inventory changed')
    body=body.replace('    f_0005C300(c);','#if '+FLAG+'\n    if (!xv_subcluster_bounds(c)) f_0005C300(c);\n#else\n    f_0005C300(c);\n#endif')
    body=body.replace('L_00052ED7:','L_00052ED7:\n#if '+FLAG+'\n    if (!c->r[5] && xv_subcluster_publish(c)) goto L_00052F19;\n#endif')
    return ('/* XV_TYPED_SUBCLUSTER_SCOPE: 00052E10 */\n#if '+FLAG+
            '\nextern int xv_subcluster_bounds(xctx *), xv_subcluster_publish(xctx *);\n#endif\n'+body)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('stage','xbe','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();stage=a.stage.resolve();out=a.out.resolve()
    if out.is_relative_to(ROOT) or out.is_relative_to(stage):p.error('private output required outside retained/source tree')
    proof=audit(a.xbe);matches=[]
    for file in (stage/'recomp').glob('code_*.c'):
        source=file.read_text();start=source.find('void f_00052E10(')
        if start>=0:matches.append((file,source,start,source.index('\nvoid f_',start+1)))
    if len(matches)!=1:raise ValueError('ambiguous subcluster root')
    file,source,start,end=matches[0];result=source[:start]+transform(source[start:end])+source[end:]
    out.mkdir(parents=True,exist_ok=False);(out/file.name).write_text(result)
    proof.update(unit=file.name,source_sha256=hashlib.sha256(source.encode()).hexdigest(),output_sha256=hashlib.sha256(result.encode()).hexdigest(),body_pin=PIN)
    (out/'receipt.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Prepared guarded subcluster caller; other function bodies retained.')

if __name__=='__main__':main()
