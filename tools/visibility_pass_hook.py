#!/usr/bin/env python3
"""Prepare the pinned whole subcluster-pass call; retain the original fallback."""
import argparse,hashlib,json
from pathlib import Path
from audit_subcluster_contract import audit
ROOT=Path(__file__).resolve().parents[1]
PIN='8bc7972ea8f00a8fbfaf7d66b865dfdae5d6f9c7304f9a3589d311a9b9426dec'
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('stage','xbe','out'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.out.resolve();stage=a.stage.resolve()
    if out.is_relative_to(ROOT) or out.is_relative_to(stage):p.error('private output required')
    proof=audit(a.xbe);found=[]
    for file in (stage/'recomp').glob('code_*.c'):
        s=file.read_text();start=s.find('void f_000539C0(')
        if start>=0:found.append((file,s,start,s.index('\nvoid f_',start+1)))
    if len(found)!=1:raise ValueError('ambiguous visibility consumer')
    file,s,start,end=found[0];body=s[start:end]
    if hashlib.sha256(body.encode()).hexdigest()!=PIN or body.count('    f_00052E10(c);')!=1:raise ValueError('visibility consumer changed')
    body=body.replace('    f_00052E10(c);','#if XV_NATIVE_VISIBILITY_PASS\n    if (!xv_visibility_pass(c)) f_00052E10(c);\n#else\n    f_00052E10(c);\n#endif')
    prefix='/* XV_NATIVE_VISIBILITY_PASS_SCOPE: 000539C0 */\n#if XV_NATIVE_VISIBILITY_PASS\nextern int xv_visibility_pass(xctx *);\n#endif\n'
    result=s[:start]+prefix+body+s[end:];out.mkdir(parents=True,exist_ok=False);(out/file.name).write_text(result)
    proof.update(unit=file.name,body_pin=PIN,source_sha256=hashlib.sha256(s.encode()).hexdigest(),output_sha256=hashlib.sha256(result.encode()).hexdigest())
    (out/'receipt.json').write_text(json.dumps(proof,indent=2)+'\n');print('Prepared native whole-pass boundary with original fallback')
if __name__=='__main__':main()
