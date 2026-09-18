"""Pin the existing cumulative portal body, then guard only callsite 534D5."""
import hashlib

FLAG='XV_TYPED_PORTAL_POLYGON'
PIN='546972fa8ad2f09af90eb0f054a374f60bd434d37ada25af7ac99148056bb380'

def transform(body):
    if hashlib.sha256(body.encode()).hexdigest()!=PIN:
        raise ValueError('cumulative portal body drift')
    native='    VP_SAVE(); f_000B7F10(c); VP_LOAD();'
    original='    f_000B7F10(c);'
    if body.count(native)!=1 or body.count(original)!=1 or body.count('/* 000534D5  call 000B7F10h */')!=2:
        raise ValueError('portal call inventory changed')
    def guarded(line,save,load):
        return ('#if '+FLAG+'\n'+save+'    if (!xv_portal_polygon(c)) f_000B7F10(c);\n'+load+
                '#else\n'+line+'\n#endif')
    body=body.replace(native,guarded(native,'    VP_SAVE();\n','    VP_LOAD();\n'))
    body=body.replace(original,guarded(original,'',''))
    return ('/* XV_TYPED_PORTAL_POLYGON_SCOPE: 000534D5 */\n#if '+FLAG+
            '\nextern int xv_portal_polygon(xctx *);\n#endif\n'+body)

def main():
    import argparse,json,sys
    from pathlib import Path
    root=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(root/'tools'))
    from audit_portal_clip_contract import audit
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True);p.add_argument('--xbe',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    stage=a.stage.resolve();out=a.out.resolve()
    if out.is_relative_to(root) or out.is_relative_to(stage):p.error('owned outputs must be outside source and retained stage')
    proof=audit(a.xbe,stage);matches=[]
    for f in (stage/'recomp').glob('code_*.c'):
        text=f.read_text();start=text.find('void f_000532E0(')
        if start<0:continue
        end=text.index('\nvoid f_',start+1)
        matches.append((f,text,start,end))
    if len(matches)!=1:raise ValueError('ambiguous portal root')
    f,text,start,end=matches[0];body=transform(text[start:end]);output=text[:start]+body+text[end:]
    out.mkdir(parents=True,exist_ok=False);(out/f.name).write_text(output)
    proof.update(unit=f.name,source_sha256=hashlib.sha256(text.encode()).hexdigest(),
        output_sha256=hashlib.sha256(output.encode()).hexdigest(),body_pin=PIN)
    (out/'receipt.json').write_text(json.dumps(proof,indent=2)+'\n')
    print('Prepared one guarded portal unit; all other bodies retained.')

if __name__=='__main__':main()
