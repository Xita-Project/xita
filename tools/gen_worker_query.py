#!/usr/bin/env python3
"""Generate an owned, bounded-C 56670 prefix. Generated game code stays local."""
from pathlib import Path
import argparse, hashlib, json, re, sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
IMAGE='4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
PCS=(0x56670,0x52240,0x51E90,0x11840,0xB77C0)

def rewrite_stores(text):
    # The original emitter writes through lvalue macros. Rewrite only these
    # stores; reads use typed functions and cannot accidentally dirty a byte.
    matches=list(re.finditer(r'X_(M|IMG)(8|16|32|64)\(',text))
    for m in reversed(matches):
        pos=m.end();level=1
        while level:
            level+=(text[pos]=='(')-(text[pos]==')');pos+=1
        tail=re.match(r'\s*=\s*(?!=)',text[pos:])
        if not tail:continue
        end=text.index(';',pos+tail.end())
        address=text[m.end():pos-1];value=text[pos+tail.end():end]
        text=text[:m.start()]+f'Q_W{m[2]}({address}, {value}, {int(m[1]=="IMG")})'+text[end:]
    return text

def generate(xbe,manifest,out):
    img=r.Image(str(xbe),str(manifest))
    assert hashlib.sha256(img.data).hexdigest()==IMAGE,'unsupported image'
    disc=r.Discovery(img,{},img.kernel_imports(),lambda *args:None)
    for pc in (*PCS,0x565E0,0xA92C0,0xA9330):
        disc.add_root(pc);disc.lift_function(disc.functions[pc]);disc.split_blocks(disc.functions[pc])
    emit=r.Emitter(img,disc,{},img.kernel_imports(),'unused',1,hooks=NoGameHooks())
    raw={pc:emit.emit_function(fn) for pc,fn in disc.functions.items() if pc in (*PCS,0x565E0,0xA92C0,0xA9330)}
    bodies=[]
    for pc in PCS:
        body=raw[pc]
        if pc==0x56670:body=body[:body.index('L_000566DE:')]+'L_000566DE:\n    return;\n}\n'
        original=body
        body=re.sub(r'^    uint8_t \*const (?:xram_|imgb_).*\n','',body,flags=re.M)
        body=body.replace('void f_', 'static void q_').replace('xctx *restrict c','xctx *c, Query *v')
        body=re.sub(r'\bf_([0-9A-F]{8})\(c\)',r'q_\1(c,v)',body)
        body=rewrite_stores(body)
        # Native recursion is bounded independently of guest backedge budget.
        body=body.replace('{\n','{\n    if (++v->depth>256) q_fail(v, Q_LIMIT);\n    if (v->depth>v->peak_depth) v->peak_depth=v->depth;\n',1)
        body=body.replace('return;','v->depth--; return;')
        assert re.findall(r'/\* (.*?) \*/',body)==re.findall(r'/\* (.*?) \*/',original)
        assert not re.search(r'\bf_[0-9A-F]{8}|xv_call|xv_unimpl|X_G\(',body), (hex(pc), re.findall(r'.*(?:\bf_|xv_call|xv_unimpl|X_G\().*',body))
        assert set(re.findall(r'\b(\w+)\(c(?:,|\))',body)) <= {
            'XF_Z','XF_S','XF_O','XF_C','XF_P','x_shl32','x_shr32','x_imul32',
            'x87_push','x87_pop','x87_compare','x87_load_f32','x87_store_f32',
            *(f'q_{p:08X}' for p in PCS)}
        bodies.append(body)
    text='/* Owned generated code; do not distribute. */\n'+''.join(f'static void q_{pc:08X}(xctx *, Query *);\n' for pc in PCS)+''.join(bodies)
    out.mkdir(parents=True,exist_ok=True)
    (out/'xk_worker_query_generated.inc').write_text(text)
    (out/'worker_query_original.json').write_text(json.dumps(raw))
    (out/'worker_query_axes.h').write_text('static const unsigned char axes[24]={'+','.join(map(str,img.bytes_at(0x1eaf30,24)))+'};\n')
    print('PASS: supported owned image, original instruction sequence, bounded C query closure')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--xbe',type=Path,required=True);p.add_argument('--manifest',type=Path,required=True);p.add_argument('--out',type=Path,default=ROOT/'recomp/kernel');a=p.parse_args()
    generate(a.xbe,a.manifest,a.out)
