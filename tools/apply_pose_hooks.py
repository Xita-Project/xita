#!/usr/bin/env python3
"""Apply the profile's pose scope/retirement hooks to retained owned C shards."""
import argparse
from pathlib import Path
import re
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from recompiler.xita_recomp import Image
from games.halo_ce_3925.hooks import HaloHooks

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe',required=True);p.add_argument('--manifest',required=True)
    p.add_argument('--directory',required=True,type=Path);a=p.parse_args()
    hooks=HaloHooks(Image(a.xbe,a.manifest))
    changes={};seen={address:0 for address in (0x5B4A0,0x58440,0x58CD0)}
    blocks={}
    for address in seen:
        lines=hooks.function_entry(address)
        start=lines.index('#if XV_POSE_PIPELINE')
        end=lines.index('#endif',start)
        blocks[address]='\n'.join(lines[start:end+1])+'\n'
    for path in a.directory.glob('code_*.c'):
        text=path.read_text();updated=text
        for address,block in blocks.items():
            head=f'void f_{address:08X}(xctx *restrict c)\n{{\n'
            n=updated.count(head);seen[address]+=n
            if not n:continue
            assert n==1,('duplicate function',path,address)
            # Remove only our exact previously emitted block, then install at
            # the entry. Unknown or partial markers fail before any write.
            marker='XV_POSE_SCOPE:' if address==0x5B4A0 else 'XV_POSE_RETIRE:'
            pos=updated.index(head);finish=updated.find('\nvoid ',pos+len(head))
            if finish<0:finish=len(updated)
            body=updated[pos:finish]
            if marker in body:
                assert body.count(block)==1,('changed pose hook',path,address)
                body=body.replace(block,'',1)
                updated=updated[:pos]+body+updated[finish:]
            updated=updated.replace(head,head+block,1)
        if updated!=text:changes[path]=updated
    assert all(n==1 for n in seen.values()),('missing/duplicate functions',seen)
    for path,text in changes.items():path.write_text(text)
    print('Pose hooks verified; changed',len(changes),'shards')
if __name__=='__main__':main()
