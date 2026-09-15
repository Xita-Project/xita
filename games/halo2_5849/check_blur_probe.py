#!/usr/bin/env python3
"""Independent clipped four-sample averaging and alpha-gain reference."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
from check_composition_probe import rgba,sample


def synthetic(test,unit):
    y,x=np.indices((120,160),dtype=np.uint32);phase=0 if test==1 else unit
    r=(x*17+y*7+phase*41)&255;g=(x*5+y*23+phase*53)&255;b=(x*11+y*13+phase*67)&255;a=(x+y+phase*71)&255
    if test>=3 and test!=5:
        r=(x//4*29+y//4*17+phase*37)&255;g=(x//4*11+y//4*31+phase*43)&255
        b=np.where(((x//4)^(y//4))&1,255,128);a=(x*3+y*7+phase*61)&255
    if test==7:
        r=np.full_like(x,96+unit*48);g=np.full_like(x,64+unit*32)
        b=np.full_like(x,32+unit*56);a=np.full_like(x,200+unit*16)
    return np.stack([r,g,b,a],axis=-1)/255


def compare(prepared,results,sample_results=None,encoding=None):
    vertices=np.fromfile(prepared/'blur.vertices.bin',dtype='<f4').reshape(4,7,4).astype(float)
    constants=np.fromfile(prepared/'blur.constants.bin',dtype='<f4').reshape(18,4).astype(float)
    image=rgba((prepared/'blur.texture.bin').read_bytes(),160,120)
    expected_positions=np.array([[0,0],[0,480],[640,480],[640,0]])
    if not np.array_equal(vertices[:,0,:2],expected_positions):raise ValueError('unexpected window positions')
    if not np.all(vertices[:,0,3]==vertices[0,0,3])or vertices[0,0,3]<=0:raise ValueError('nonconstant positive W required')
    if not np.all(vertices[:,1:5,3]==1):raise ValueError('unsupported projected coordinates')
    y,x=np.indices((120,160));reports=[]
    for test in range(8):
        average=0
        for unit in range(4):
            # Keep the original 640x480 window quad; only its visible 160x120
            # portion is rasterized. A smaller replacement quad is incorrect.
            attr=vertices[:,unit+1];u=attr[0,0]+(x+.5)*(attr[3,0]-attr[0,0])/640
            v=attr[0,1]+(y+.5)*(attr[1,1]-attr[0,1])/480
            if test==5:u=u+.5;v=v+.25
            average=average+sample(image if test==0 else synthetic(test,unit),u/160,v/120)/4
        gain=constants[10,3]
        if test==6:gain=.125
        if test==7:gain=1
        # Original RGB pairwise averages; alpha sums the two half-averages
        # times c1[2].a, then final unsigned output clamps the result.
        expected=np.concatenate([average[...,:3],np.clip(average[...,3:4]*2*gain,0,1)],axis=-1)
        wanted=np.floor(expected*255+.5).astype(int)
        raw=(results/f'blur-probe-{test}.bin').read_bytes()
        if len(raw)!=160*120*4:raise ValueError('incomplete GPU output')
        actual=np.rint(rgba(raw,160,120)*255).astype(int);error=np.abs(wanted-actual)
        reports.append(dict(test=test,pixels=160*120,max_error=error.max(axis=(0,1)).tolist(),
            outside_1=int(np.any(error>1,axis=-1).sum()),
            outside_bound=int(np.any(error>np.array([1,1,1,2]),axis=-1).sum()),nonblack=int(np.any(actual[...,:3]!=0,axis=-1).sum()),
            first_expected=wanted[0,0].tolist(),first_actual=actual[0,0].tolist()))
    winding=(results/'blur-probe-3.bin').read_bytes()==(results/'blur-probe-4.bin').read_bytes()
    isolation=isolate(prepared,results,sample_results,encoding) if sample_results is not None else None
    return dict(passed=bool(winding and isolation and isolation['passed'] and all(not r['outside_bound'] for r in reports)),pixels=8*160*120,
                sample_isolation=isolation,
                opposite_winding_identical=winding,fixtures=reports,
                inputs={f.name:hashlib.sha256(f.read_bytes()).hexdigest()for f in prepared.glob('*.bin')})

def decode_sampler(raw,encoding):
    if encoding not in ('half','vita3k-unorm16'):raise ValueError('explicit RGBA16 readback encoding required')
    if len(raw)!=160*120*8:raise ValueError('incomplete sampler output')
    actual=np.frombuffer(raw,dtype='<u2' if encoding=='vita3k-unorm16' else '<f2').astype(float).reshape(120,160,4)
    if encoding=='vita3k-unorm16':actual/=65535
    if not np.all(np.isfinite(actual))or np.any(actual<0)or np.any(actual>1):raise ValueError('invalid normalized sampler output')
    return actual


def isolate(prepared,results,sample_results,encoding):
    if encoding not in ('half','vita3k-unorm16'):raise ValueError('explicit RGBA16 readback encoding required')
    vertices=np.fromfile(prepared/'blur.vertices.bin',dtype='<f4').reshape(4,7,4).astype(float)
    constants=np.fromfile(prepared/'blur.constants.bin',dtype='<f4').reshape(18,4).astype(float)
    image=rgba((prepared/'blur.texture.bin').read_bytes(),160,120)
    y,x=np.indices((120,160));samplers=[];combiners=[]
    for test in range(8):
        measured=[]
        for unit in range(4):
            raw=(sample_results/f'blur-probe-sample-{test}-{unit}.rgba16').read_bytes()
            actual=decode_sampler(raw,encoding)
            measured.append(actual)
            attr=vertices[:,unit+1];u=attr[0,0]+(x+.5)*(attr[3,0]-attr[0,0])/640
            v=attr[0,1]+(y+.5)*(attr[1,1]-attr[0,1])/480
            if test==5:u+=.5;v+=.25
            expected=sample(image if test==0 else synthetic(test,unit),u/160,v/120)
            error=np.abs(expected-actual)*255
            samplers.append(dict(test=test,unit=unit,max_error=error.max(axis=(0,1)).tolist(),outside_1=int(np.any(error>1,axis=-1).sum()),outside_2=int(np.any(error>2,axis=-1).sum())))
        average=sum(measured)/4;gain=.125 if test==6 else 1 if test==7 else constants[10,3]
        expected=np.concatenate([average[...,:3],np.clip(average[...,3:4]*2*gain,0,1)],axis=-1)
        output=np.rint(rgba((results/f'blur-probe-{test}.bin').read_bytes(),160,120)*255).astype(int)
        error=np.abs(np.floor(expected*255+.5).astype(int)-output)
        combiners.append(dict(test=test,max_error=error.max(axis=(0,1)).tolist(),outside_1=int(np.any(error>1,axis=-1).sum())))
    return dict(passed=all(not r['outside_2'] for r in samplers) and all(not r['outside_1'] for r in combiners),encoding=encoding,samplers=samplers,combiners=combiners,
                note='Independent ideal sampler comparison and CPU averaging/gain from measured samples; not bit-exact NV2A hardware')


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--prepared',type=Path,required=True);ap.add_argument('--results',type=Path,required=True);ap.add_argument('--sample-results',type=Path,required=True);ap.add_argument('--sample-encoding',choices=('half','vita3k-unorm16'),required=True);a=ap.parse_args()
    result=compare(a.prepared,a.results,a.sample_results,a.sample_encoding);print(json.dumps(result,indent=2));sys.exit(0 if result['passed']else 1)
