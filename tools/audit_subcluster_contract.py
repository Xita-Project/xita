#!/usr/bin/env python3
"""Audit the owned Halo 3925 subcluster bounds data boundary; emit metadata only."""
import argparse
import hashlib
import json
from pathlib import Path
from audit_visibility_dispatch import verify_image
from recompiler.xita_recomp import Image
from iced_x86 import Decoder,InstructionInfoFactory,Mnemonic,OpKind,OpAccess,Register

def audit(path):
    proof=verify_image(path);img=Image(str(path));factory=InstructionInfoFactory()
    spans=[(0x52e10,0x140,'8fad0bb52b35ae37187dc52f6be3fdde70c4fa9e4019b4ccc2d2982f9e1e7248'),
           (0x5c300,0x2dd,'5e463d77ea6ed255323f310d40cf3f7e847c08e7a6a71841b12545b1f937e1ab')]
    decoded={}
    for pc,size,expected in spans:
        raw=img.bytes_at(pc,size);assert hashlib.sha256(raw).hexdigest()==expected
        ins=list(Decoder(32,raw,ip=pc));assert not any(i.is_invalid for i in ins) and ins[-1].next_ip==pc+size
        decoded[pc]=ins
    caller={i.ip:i for i in decoded[0x52e10]}
    assert caller[0x52ebf].mnemonic==Mnemonic.PUSH and caller[0x52ebf].immediate8==0
    call=caller[0x52ec1];assert call.mnemonic==Mnemonic.CALL and call.near_branch_target==0x5c300
    test=caller[0x52ec6];assert test.mnemonic==Mnemonic.TEST and test.op0_register==Register.AX and test.op1_register==Register.AX
    leaf=decoded[0x5c300];assert not any(i.mnemonic==Mnemonic.CALL for i in leaf)
    writes=[];backwards=[]
    write_access={OpAccess.WRITE,OpAccess.COND_WRITE,OpAccess.READ_WRITE,OpAccess.READ_COND_WRITE}
    for ins in leaf:
        for mem in factory.info(ins).used_memory():
            if mem.access in write_access:
                assert mem.base==Register.ESP and mem.index==Register.NONE
                writes.append(hex(ins.ip))
        if ins.op0_kind==OpKind.NEAR_BRANCH32 and ins.near_branch_target<ins.ip:
            backwards.append([hex(ins.ip),hex(ins.near_branch_target)])
    assert backwards==[['0x5c4ea','0x5c3f2'],['0x5c5c1','0x5c530'],['0x5c5ca','0x5c507']]
    proof.update(leaf='0x5c300',caller='0x52e10',call='0x52ec1',stack_only_write_sites=writes,
                 backward_branches=backwards,extra_reverse_corner_argument=0,
                 inputs=dict(side_planes_offset='0x78',side_plane_count=4,enclosing_bounds_offset='0x128',box_bytes=24),
                 observations=['Leaf writes only to its stack in the decoded original; aliases must still be excluded.',
                   'Broad AABB rejection deducts zero loop backedges; otherwise the selected path deducts seven.',
                   'The caller consumes AX as a visibility predicate and retains serial surface-bit publication.',
                   'The surface count cap and cooperative yields prohibit arbitrary reordering of publication.'],
                 limits=['Not a concurrent-writer proof or production admission; mapping, alias, diagnostics, owner and FP controls remain required.'])
    return proof

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--xbe',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    r=audit(a.xbe)
    with a.out.open('x') as f:json.dump(r,f,indent=2);f.write('\n')
    print('PASS pinned caller/leaf, zero reverse-corner argument, stack-only writes, backward-branch inventory')

if __name__=='__main__':main()
