#!/usr/bin/env python3
"""Independent one-sampler screen-blend GPU reference; no guest command changes."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
from check_composition_probe import rgba,sample


def synthetic(test):
    y,x=np.indices((120,160),dtype=np.uint32)
    r=(x*17+y*7)&255;g=(x*5+y*23)&255;b=(x*11+y*13)&255;a=(x*3+y*7)&255
    if test>=2:
        r=(x//4*29+y//4*17)&255;g=(x//4*11+y//4*31)&255;b=np.where(((x//4)^(y//4))&1,255,128)
    if test==6:r=g=b=np.full_like(x,255)
    if test==8:a=255-a
    return np.stack([r,g,b,a],axis=-1)/255


def destination():
    y,x=np.indices((480,640),dtype=np.uint32)
    return np.stack([(x*3+y*17)&255,(x*11+y*13)&255,(x*7+y*5)&255,(x*19+y*23)&255],axis=-1)/255


def equations(sampled,gain,dest):
    src=np.concatenate([np.clip(2*sampled[...,:3]*gain,0,1),np.zeros(sampled.shape[:-1]+(1,))],axis=-1)
    return src,np.clip(src*(1-dest)+dest,0,1)


def compare(prepared,results,source_results):
    vertices=np.fromfile(prepared/'blend.vertices.bin',dtype='<f4').reshape(4,7,4).astype(float)
    gain=np.fromfile(prepared/'blend.constants.bin',dtype='<f4').reshape(18,4)[0,:3].astype(float)
    image=rgba((prepared/'blend.texture.bin').read_bytes(),160,120)
    if not np.array_equal(vertices[:,0,:2],[[0,0],[0,480],[640,480],[640,0]]):raise ValueError('unexpected window quad')
    if not np.all(vertices[:,0,3]==vertices[0,0,3])or vertices[0,0,3]<=0:raise ValueError('requires constant positive W')
    if not np.all(vertices[:,1,3]==1):raise ValueError('unexpected projected coordinates')
    y,x=np.indices((480,640));dest=destination();reports=[]
    def output(directory,name):
        raw=(directory/name).read_bytes()
        if len(raw)!=640*480*4:raise ValueError('incomplete output')
        return rgba(raw,640,480)
    for test in range(10):
        attr=vertices[:,1];u=attr[0,0]+(x+.5)*(attr[3,0]-attr[0,0])/640
        v=attr[0,1]+(y+.5)*(attr[1,1]-attr[0,1])/480
        if test==4:u=u+.5;v=v+.25
        factors=gain.copy()
        if test==5:factors[:]=[.25,.5,.75]
        if test==6:factors[:]=1
        if test==7:factors[:]=0
        src,blended=equations(sample(image if test==0 else synthetic(test),u/160,v/120),factors,dest)
        if test==9:src=blended=dest
        actual=output(results,f'blend-probe-{test}.bin');source=output(source_results,f'blend-probe-source-{test}.bin')
        wanted=np.floor(blended*255+.5);error=np.abs(wanted-np.rint(actual*255))
        source_error=np.abs(np.floor(src*255+.5)-np.rint(source*255))
        # UCHAR4 fragment output and destination blending are separate rounding
        # stages. Require the measured-source blend to match exactly; retain
        # the ideal one-level failures and the one-level source bound.
        measured=source if test==9 else np.clip(source*(1-dest)+dest,0,1)
        measured_error=np.abs(np.floor(measured*255+.5)-np.rint(actual*255))
        reports.append(dict(test=test,pixels=640*480,max_error=error.max(axis=(0,1)).tolist(),
            source_max_error=source_error.max(axis=(0,1)).tolist(),measured_source_blend_max_error=measured_error.max(axis=(0,1)).tolist(),
            outside_1=int(np.any(error>1,axis=-1).sum()),outside_bound=int(np.any(error>(2 if test==1 else 1),axis=-1).sum()),
            full_bound=2 if test==1 else 1,source_outside_1=int(np.any(source_error>1,axis=-1).sum()),
            measured_outside_1=int(np.any(measured_error>1,axis=-1).sum()),
            destination_alpha_exact=bool(np.array_equal(actual[...,3],dest[...,3])),
            destination_exact=bool(np.array_equal(actual,dest))))
    winding=(results/'blend-probe-2.bin').read_bytes()==(results/'blend-probe-3.bin').read_bytes()
    alpha_unused=(results/'blend-probe-2.bin').read_bytes()==(results/'blend-probe-8.bin').read_bytes()
    return dict(passed=bool(winding and alpha_unused and all(not(r['outside_bound']or r['source_outside_1']or any(r['measured_source_blend_max_error']))and r['destination_alpha_exact']for r in reports)
                and all(reports[i]['destination_exact']for i in (0,7,9))),pixels=10*640*480,
        opposite_winding_identical=winding,unused_source_alpha_identical=alpha_unused,fixtures=reports,
        inputs={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in prepared.glob('*.bin')})

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('prepared','results','source-results'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();report=compare(a.prepared,a.results,a.source_results)
    print(json.dumps(report,indent=2));sys.exit(0 if report['passed']else 1)
