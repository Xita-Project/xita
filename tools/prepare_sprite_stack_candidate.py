#!/usr/bin/env python3
"""Prepare a private, unqualified stack-translation experiment for 597CB.

No production hook/default. Preserve every guest operation and refresh the
stack mapping after scheduler handoffs; crossing stacks use original accesses.
"""
import argparse, hashlib, json, re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--shard',type=Path,required=True)
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
s=a.shard.read_text().split('void f_000597CB(xctx *restrict c)\n',1)[1].split('\nvoid f_',1)[0]
pin='91b7b7807aadde2948e03f00ca66739c7a56e6a0ee2ef90a5abc884e9112df74'
assert hashlib.sha256(s.encode()).hexdigest()==pin,'unreviewed body'
front,tail=s.split('    c->r[3] = X_POP32();',1)
assert not re.search(r'c->r\[4\]\s*(?:=|\+=|-=)|X_PUSH32|f_[0-9A-F]+\(c\)',front)
assert front.count('X_PREEMPT()')==1
# Cache only the page translation, never the stack values. A handoff can change
# both ESP and mappings. The slow path retains original unaligned/page semantics.
front=front.replace('{\n','{\n    uint8_t *sprite_stack=0; SPRITE_REFRESH();\n',1)
front=front.replace('X_PREEMPT()', 'SPRITE_PREEMPT()')
pat=r'\(c->r\[4\]\+0x([0-9A-F]+)u\)'
counts={}
front,counts['float_loads']=re.subn(r'x87_load_f32\(c, '+pat+r'\)',r'sprite_load(SPRITE_PTR(0x\1u))',front)
front,counts['float_stores']=re.subn(r'x87_store_f32\(c, '+pat+r', ([^;]+)\);',r'sprite_store(SPRITE_PTR(0x\1u), \2);',front)
front,counts['word_accesses']=re.subn(r'X_M32\('+pat+r'\)',r'(*(xu32_u *)SPRITE_PTR(0x\1u))',front)
pre='''#include "xv_x86rt.h"
#include "xv_x87reg.h"
void f_0005BA10(xctx *);
static inline double sprite_load(const void *p) { float v; memcpy(&v,p,4); return (double)v; }
static inline void sprite_store(void *p,double v) { float f=(float)v; memcpy(p,&f,4); }
#define SPRITE_REFRESH() do { sprite_stack=((c->r[4]&4095u)<=4096u-0xB8u) ? X_G(c->r[4]) : 0; } while(0)
#define SPRITE_PTR(off) (sprite_stack ? sprite_stack+(off) : X_G(c->r[4]+(off)))
#define SPRITE_PREEMPT() do { if(--c->preempt<=0) { xv_preempt(c); SPRITE_REFRESH(); } } while(0)
'''
# Diagnostics may remap memory between individual accesses; do not cache there.
body=front+'    c->r[3] = X_POP32();'+tail
body=body.replace('{\n','{\n#ifdef XV_CHECK_GUEST_ADDRESS\n    original(c); return;\n#endif\n',1)
a.out.mkdir(parents=True,exist_ok=False)
(a.out/'reference.c').write_text(pre+'void original(xctx *restrict c)\n'+s+'\nvoid candidate(xctx *restrict c)\n'+body)
(a.out/'audit.json').write_text(json.dumps({'body_sha256':pin,'replacements':counts,'qualified':False,'production_enabled':False},indent=2)+'\n')
print(counts)
