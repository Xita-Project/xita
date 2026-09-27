#!/usr/bin/env python3
"""Prepare a private GPR-local experiment for the pinned visibility-mask body.
No production hook; guest source remains in the requested private directory.
"""
import argparse,hashlib,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--reference',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--local-context',action='store_true');a=p.parse_args()
s=a.reference.read_text().split('void f_00053E90(xctx *restrict c)\n',1)[1].split('\nvoid f_',1)[0]
if hashlib.sha256(s.encode()).hexdigest()!='325e0b3eb548cb30a5f46f0d5922c1a635a9ad2c859a65cabfb1204b469b3b84':raise SystemExit('unsupported retained body')
helpers=set(re.findall(r'\b(\w+)\(c(?:,|\))',s))
if not helpers <= {'XF_C','XF_Z','XF_S','XF_O','x_shl32'}:raise SystemExit(f'unreviewed context helpers: {helpers}')
load=' '.join(f'r{i}=c->r[{i}];' for i in range(8));save=' '.join(f'c->r[{i}]=r{i};' for i in range(8))
b=re.sub(r'c->r\[([0-7])\]',r'r\1',s)
for macro in ('R16','R8L','PUSH32','POP32','PREEMPT'):b=b.replace('X_'+macro+'(', 'MASK_'+macro+'(')
b=b.replace('return;', 'do { MASK_SAVE(); return; } while (0);')
b=b.replace('{\n','{\n    uint32_t '+','.join(f'r{i}=c->r[{i}]' for i in range(8))+';\n',1)
macros=f'''
#define MASK_SAVE() do {{ {save} }} while(0)
#define MASK_LOAD() do {{ {load} }} while(0)
#define MASK_R16(i) (*(uint16_t *)&r##i)
#define MASK_R8L(i) (*(uint8_t *)&r##i)
#define MASK_PUSH32(v) do {{ uint32_t v__=(v);r4-=4;X_W32(r4)=v__; }} while(0)
#define MASK_POP32() ({{ uint32_t v__=X_M32(r4);r4+=4;v__; }})
#define MASK_PREEMPT() do {{ if(--c->preempt<=0) {{MASK_SAVE();xv_preempt(c);MASK_LOAD();}} }} while(0)
'''
a.out.mkdir(parents=True,exist_ok=False)
if a.local_context:
    fields=('f_kind','f_op1','f_op2','f_res','f_bits','f_cf_override','f_cf','f_of_override','f_of','preempt')
    publish='memcpy(owner->r,c->r,sizeof owner->r); '+ ' '.join(f'owner->{f}=c->{f};' for f in fields)
    b=s.replace('{\n','{\n    xctx state=*owner; xctx *restrict c=&state;\n',1)
    b=b.replace('XV_PHASE_SCOPE(c,', 'XV_PHASE_SCOPE(owner,')
    b=b.replace('return;', 'do { MASK_PUBLISH(); return; } while(0);')
    b=b.replace('X_PREEMPT()', 'MASK_CONTEXT_PREEMPT()')
    macros='\n#define MASK_PUBLISH() do { '+publish+' } while(0)\n'
    macros+='\n#define MASK_CONTEXT_PREEMPT() do {if(--c->preempt<=0){MASK_PUBLISH();xv_preempt(owner);*c=*owner;}}while(0)\n'
    signature='void candidate(xctx *restrict owner)\n'
else:
    signature='void candidate(xctx *restrict c)\n'
(a.out/'reference.c').write_text('#include "kernel/xk_surface_mask_scan.h"\nvoid original(xctx *restrict c)\n'+s+macros+'\n'+signature+b)
print('prepared pinned mask body; local context:',a.local_context)
