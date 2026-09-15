#!/usr/bin/env python3
"""Independent DPH coordinate transform and clamped luminance/tint reference."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
from check_composition_probe import rgba,sample


def synthetic(test):
    y,x=np.indices((480,640),dtype=np.uint32)
    r=(x*17+y*7)&255;g=(x*5+y*23)&255;b=(x*11+y*13)&255;a=(x*3+y*7)&255
    if test>=2:r=(x//4*29+y//4*17)&255;g=(x//4*11+y//4*31)&255;b=np.where(((x//4)^(y//4))&1,255,128)
    if test==8:r=g=b=np.full_like(x,255)
    if test==9:a=255-a
    return np.stack([r,g,b,a],axis=-1)/255


def dph(position,constant):
    return (position[...,:3]*constant[:3]).sum(axis=-1)+constant[3]


def tint(sampled,weight,color):
    luminance=np.clip((sampled[...,:3]*weight).sum(axis=-1),-1,1)
    return np.concatenate([np.clip(luminance[...,None]*color,0,1),np.zeros(sampled.shape[:-1]+(1,))],axis=-1)


def compare(prepared,results):
    vertices=np.fromfile(prepared/'luma.vertices.bin',dtype='<f4').reshape(4,8,4).astype(float)
    vc=np.fromfile(prepared/'luma.vertex-constants.bin',dtype='<f4').reshape(8,4).astype(float)
    pc=np.fromfile(prepared/'luma.constants.bin',dtype='<f4').reshape(18,4).astype(float)
    image=rgba((prepared/'luma.texture.bin').read_bytes(),640,480)
    if not np.array_equal(vertices[:,0,:2],[[0,0],[0,480],[640,0],[640,480]]):raise ValueError('unexpected strip positions')
    if not np.all(vertices[:,0,3]==vertices[0,0,3])or vertices[0,0,3]<=0:raise ValueError('nonconstant position W')
    y,x=np.indices((480,640));position=vertices[0,1]+(x[...,None]+.5)*(vertices[2,1]-vertices[0,1])/640+(y[...,None]+.5)*(vertices[1,1]-vertices[0,1])/480
    reports=[]
    for test in range(10):
        constants=vc.copy();weight=pc[0,:3].copy()
        if test==4:constants[0,3]+=.5;constants[1,3]+=.25
        if test==5:constants[0,:2]=[0,np.float32(640/480)];constants[1,:2]=[.75,0]
        if test==6:constants[0,0]=constants[1,1]=.5;constants[0,3]=100;constants[1,3]=80;weight[:]=np.array([.2,.4,.6],dtype=np.float32)
        if test==7:weight[:]=0
        u=dph(position,constants[0]);v=dph(position,constants[1])
        expected=tint(sample(image if test==0 else synthetic(test),u/640,v/480),weight,pc[16,:3])
        raw=(results/f'luma-probe-{test}.bin').read_bytes()
        if len(raw)!=640*480*4:raise ValueError('incomplete output')
        actual=np.rint(rgba(raw,640,480)*255).astype(int);wanted=np.floor(expected*255+.5).astype(int);error=np.abs(wanted-actual)
        reports.append(dict(test=test,pixels=640*480,max_error=error.max(axis=(0,1)).tolist(),outside_1=int(np.any(error>1,axis=-1).sum()),
            nonblack=int(np.any(actual[...,:3]!=0,axis=-1).sum()),alpha_zero=bool(np.all(actual[...,3]==0)),
            first_expected=wanted[0,0].tolist(),first_actual=actual[0,0].tolist()))
    winding=(results/'luma-probe-2.bin').read_bytes()==(results/'luma-probe-3.bin').read_bytes()
    unused_alpha=(results/'luma-probe-2.bin').read_bytes()==(results/'luma-probe-9.bin').read_bytes()
    return dict(passed=bool(winding and unused_alpha and all(not r['outside_1']and r['alpha_zero']for r in reports)),
        pixels=10*640*480,opposite_winding_identical=winding,unused_alpha_identical=unused_alpha,fixtures=reports,
        inputs={f.name:hashlib.sha256(f.read_bytes()).hexdigest()for f in prepared.glob('*.bin')})

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('prepared','results'):ap.add_argument('--'+name,type=Path,required=True)
    a=ap.parse_args();report=compare(a.prepared,a.results);print(json.dumps(report,indent=2));sys.exit(0 if report['passed']else 1)
