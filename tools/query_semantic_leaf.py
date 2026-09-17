"""Query-only reconstruction of one qualified SSE vertex-distance interval.

No shared header, caller, solver, guest load/store, or observer is changed.
The authored header is copied to the owned generated directory and included
only by query_fusion.c. Object qualification pins actual scalar VFP order.
"""
from pathlib import Path
import hashlib

ORIGINAL='''    for(unsigned i=0;i<4;i++)c->xmm[0][i]-=c->xmm[1][i];
    for(unsigned i=0;i<4;i++)c->xmm[0][i]*=c->xmm[0][i];
    c->xmm[2][0]=c->xmm[0][0];
    x_shufps(c,c->xmm[0],c->xmm[0],14);
    c->xmm[2][0]+=c->xmm[0][0];
    x_shufps(c,c->xmm[0],c->xmm[0],57);
    c->xmm[2][0]+=c->xmm[0][0];X_MF32(q0)=c->xmm[2][0];'''
REPLACEMENT='''    nq_semantic_vertex_distance(c);
    X_MF32(q0)=c->xmm[2][0];'''
HEADER=Path(__file__).with_suffix('.h')
OUTPUT='query_semantic_leaf.h'
# Pins the reviewed VFP operand order as well as source/caller eligibility.
RETAINED_TEXT_SHA256='dedc7a8c7aab35f03bb14d074e23c852208b5715f6deadef7b9a5f604478ad98'

def sha(data):return hashlib.sha256(data).hexdigest()
def state_recipe():
    lanes=['xx','padding','yy','zz']
    def shuffle(values,imm):
        return [values[imm&3],values[(imm>>2)&3],values[(imm>>4)&3],values[(imm>>6)&3]]
    first=shuffle(lanes,14);final=shuffle(first,57)
    assert first==['yy','zz','xx','xx'] and final==['zz','xx','xx','yy']
    assert ORIGINAL.count('X_MF32(q0)=c->xmm[2][0]')==1
    assert REPLACEMENT.count('X_MF32(q0)=c->xmm[2][0]')==1
    assert not any(token in ORIGINAL for token in ('X_PREEMPT','X_PUSH32','X_POP32','x87_'))
    return dict(xmm0=final,xmm1='unchanged',xmm2_low='(yy+xx)+zz with retained VFP operand order',
                xmm2_high='unchanged',other_context='unchanged',guest_stores=['X_MF32(q0)=xmm2.low'],
                observation_points_inside_interval=0,fp_ops=['sub32']*4+['mul32']*4+['add32']*2)
def transform(source,enabled):
    if source.count(ORIGINAL)!=1:raise ValueError('typed vertex interval drift')
    if source.index(ORIGINAL)<source.index('NQ_CV_vertex:'):raise ValueError('wrong vertex interval')
    if not enabled:return source
    marker='__attribute__((noinline)) void query_fused_172c95_171f94('
    if source.count(marker)!=1:raise ValueError('query entry drift')
    changed=source.replace(ORIGINAL,REPLACEMENT)
    changed=changed.replace(marker,'#include "'+OUTPUT+'"\n'+marker)
    restored=changed.replace('#include "'+OUTPUT+'"\n','').replace(REPLACEMENT,ORIGINAL)
    if restored!=source:raise ValueError('nonlocal source edit')
    return changed


def generate(source):
    if 'query_f32_primitives/xv_recomp_protos.h' not in source:
        raise ValueError('query semantic leaf requires query f32 inline')
    header=HEADER.read_text()
    if header.count('nq_semantic_vertex_distance(xctx *c)')!=1:
        raise ValueError('semantic helper declaration drift')
    changed=transform(source,True)
    return changed,{OUTPUT:header},dict(
        authored_header_sha256=sha(header.encode()),
        original_interval_sha256=sha(ORIGINAL.encode()),
        qualified_baseline_text_sha256=RETAINED_TEXT_SHA256,
        state_recipe=state_recipe())
