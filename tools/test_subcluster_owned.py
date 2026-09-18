#!/usr/bin/env python3
"""Use owned map subcluster boxes with synthetic views in the ARM classifier oracle.

All map geometry stays outside Git. These are generated views, not hardware
captures or a measurement of the number of boxes visible during gameplay.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from test_subcluster_bounds import Machine
from audit_visibility_dispatch import audit_bsp
from recompiler.halo_map import HaloMap,bsp_header

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf',required=True,type=Path);p.add_argument('--maps',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=False);m=Machine(a.elf);reports=[];total=0
    for path in sorted(a.maps.glob('*.map')):
        g=HaloMap(str(path));record=dict(map=g.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bsps=[])
        for bsp in g.structure_bsps():
            audit=audit_bsp(g,bsp);h=bsp_header(g,bsp);base=bsp['load_address'];start=bsp['file_offset']
            def offset(address):return start+address-base
            count,clusters=struct.unpack_from('<II',g.data,offset(h['sbsp_struct_addr']+0x134))
            boxes=[]
            for i in range(count):
                n,sub=struct.unpack_from('<II',g.data,offset(clusters+i*0x68+0x34))
                for j in range(n):
                    b=struct.unpack_from('<6f',g.data,offset(sub+j*36))
                    if not all(math.isfinite(v) for v in b) or any(b[k]>b[k+1] for k in (0,2,4)):
                        raise ValueError('map bounds violate finite ordered contract')
                    boxes.append(b)
            # Every box is exercised; plane normals span all sign combinations.
            # A moving finite frustum center provides intersecting/rejected cases.
            outcomes=[0,0,0];cost=[0,0]
            for i,b in enumerate(boxes):
                center=[(b[k]+b[k+1])*.5 for k in (0,2,4)]
                scale=max(1.,max(b[k+1]-b[k] for k in (0,2,4)))
                for view in range(2):
                    sign=-1 if i&1 else 1
                    normals=[[sign,.25,-.5],[-sign,.5,.25],[.25,sign,.5],[.5,-sign,-.25]]
                    eye=[center[k]+(view*2-1)*scale*.4*(k-1) for k in range(3)]
                    planes=[v+[sum(v[k]*eye[k] for k in range(3))+scale*(.15 if view else 1.)] for v in normals]
                    spec=dict(box=b,planes=planes,frustum_box=sum(([x-scale*2,x+scale*2] for x in eye),[]),split=i%2,top=i%8)
                    mode=(i%16)<<22
                    old=m.run('f_0005C300',spec,mode);new=m.run('xs_test_bounds',spec,mode)
                    if any(old[k]!=new[k] for k in ('classification','budget','sp')):
                        (a.out/'failure.json').write_text(json.dumps(dict(map=g.name,bsp=bsp['index'],box=i,spec=spec,mode=mode,original=old,candidate=new),indent=2)+'\n')
                        raise AssertionError('owned bounds mismatch')
                    outcomes[old['classification']]+=1;cost[0]+=old['instructions'];cost[1]+=new['instructions'];total+=1
            record['bsps'].append(dict(index=bsp['index'],boxes=len(boxes),outcomes=outcomes,instructions=cost,path=audit['path']))
        reports.append(record)
        (a.out/'progress.json').write_text(json.dumps(dict(cases=total,maps=reports),indent=2)+'\n')
        print('PASS',g.name,sum(b['boxes'] for b in record['bsps']),'boxes',flush=True)
    if not reports:raise ValueError('no owned maps')
    (a.out/'result.json').write_text(json.dumps(dict(result='PASS',cases=total,maps=reports,scope='Owned finite ordered boxes, two synthetic views per box, modes distributed across boxes; no actual visibility lists or hardware timings.'),indent=2)+'\n')

if __name__=='__main__':main()
