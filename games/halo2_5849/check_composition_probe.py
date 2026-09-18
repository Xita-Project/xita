#!/usr/bin/env python3
"""Independent six-stage equations and sampler/blend reference for private probes."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import numpy as np
from check_screen_probe import decode,synthetic_blocks


def rgba(data,width,height):
    words=np.frombuffer(data,dtype='<u4').reshape(height,width)
    return np.stack([(words>>shift)&255 for shift in (16,8,0,24)],axis=-1)/255


def sample(image,u,v):
    height,width=image.shape[:2]
    s,t=np.clip(u*width-.5,0,width-1),np.clip(v*height-.5,0,height-1)
    x,y=s.astype(int),t.astype(int);f,g=(s-x)[...,None],(t-y)[...,None]
    nx,ny=np.minimum(x+1,width-1),np.minimum(y+1,height-1)
    return (image[y,x]*(1-f)+image[y,nx]*f)*(1-g)+(image[ny,x]*(1-f)+image[ny,nx]*f)*g


def equations(c,t2,t3):
    # Stage0: signed bias/double, then clamp each register component.
    signed=np.clip((t2+.5-c[0])*2,-1,1)
    # Stage1 reads unsigned identity, which saturates signed register values.
    product=t3[...,:3]*np.clip(signed[...,:3],0,1)
    alpha_product=t3[...,3]*np.clip(signed[...,3],0,1)
    # Stage2 RGB dot products are replicated to v0/v1.rgb; independent alpha
    # produces v0.a from the old r0/r1 blue components.
    d0=np.clip(np.sum(product*c[2,:3],axis=-1),-1,1)
    d1=np.clip(np.sum(product*c[10,:3],axis=-1),-1,1)
    v0a=(1-product[...,2])*(1-alpha_product)
    # Stage3 reads the previously written blue components, not v0.a.
    v1a=(1-np.clip(d0,0,1))*(1-np.clip(d1,0,1))
    # Stage3's t0.a product has no subsequent live consumer in this shader.
    # Stages4/5 mix the two colors with the independently produced alpha.
    a=v0a*v1a
    color=t2[...,:3]*(a[...,None]*c[4,:3]+(1-a[...,None])*c[12,:3])
    return np.clip(color*c[16,:3],0,1),np.clip(color[...,2],0,1)


def compare(prepared,results,source_results=None):
    c=np.fromfile(prepared/'composition.constants.bin',dtype='<f4').reshape(18,4)
    vertices=np.fromfile(prepared/'composition.vertices.bin',dtype='<f4').reshape(4,7,4)
    captured0=rgba((prepared/'composition.texture0.bin').read_bytes(),640,480)
    captured3=rgba((prepared/'composition.texture3.bin').read_bytes(),320,240)
    blocks=(prepared/'composition.texture2.bin').read_bytes()
    y,x=np.indices((480,640),dtype=np.uint32)
    synth0=np.stack([(x*11+y*23)&255,(x*37+y*19)&255,(x*13+y*17)&255,(x*7+y*3)&255],axis=-1)/255
    yy,xx=np.indices((240,320),dtype=np.uint32)
    synth3=np.stack([(xx*3+yy*19)&255,(xx*7+yy*23)&255,(xx*11+yy*13)&255,(xx*17+yy*29)&255],axis=-1)/255
    reports=[];controls=[]
    for test in range(8):
        t0=synth0 if test else captured0
        image=decode(synthetic_blocks() if 3<=test<=6 else blocks)
        image3=synth3 if 3<=test<=6 else captured3
        high=(t0[...,3]*65280+t0[...,0]*255)/65535
        low=(t0[...,1]*65280+t0[...,2]*255)/65535
        if test>=2:u,v=high,low
        else:
            u=vertices[0,2,0]*high+vertices[0,2,1]*low+vertices[0,2,2]
            v=vertices[0,3,0]*high+vertices[0,3,1]*low+vertices[0,3,2]
        t2=sample(image,u,v)
        t3=sample(image3,(x+.5)/640,(y+.5)/480)
        rgb,alpha=equations(c,t2,t3)
        destination=np.zeros((480,640,4))
        if test>=4:destination=np.where(((x^y)&1)[...,None],[0x50,0x30,0x10,0x80],[0x20,0x10,0x60,0xFF])/255
        expected=np.concatenate([np.clip(rgb+destination[...,:3]*(1-alpha[...,None]),0,1),
                                 destination[...,3,None]*(1-alpha[...,None])],axis=-1)*255
        if test==6:expected=image[y//60,x//80]*255
        if test==7:expected=captured3[y//2,x//2]*255
        wanted=np.floor(expected+.5).astype(int)
        raw=(results/f'composition-probe-{test}.bin').read_bytes()
        if len(raw)!=640*480*4:raise ValueError('incomplete output')
        actual=np.rint(rgba(raw,640,480)*255).astype(int)
        error=np.abs(wanted-actual)
        if source_results is not None and test<6:
            source_data=(source_results/f'composition-probe-source-{test}.bin').read_bytes()
            if len(source_data)!=640*480*4:raise ValueError('incomplete source control')
            source=rgba(source_data,640,480)
            source_expected=np.concatenate([rgb,alpha[...,None]],axis=-1)
            source_error=np.abs(np.floor(source_expected*255+.5)-np.rint(source*255))
            blend=np.concatenate([np.clip(source[...,:3]+destination[...,:3]*(1-source[...,3,None]),0,1),
                                  destination[...,3,None]*(1-source[...,3,None])],axis=-1)
            blend_error=np.abs(np.floor(blend*255+.5)-actual)
            controls.append(dict(test=test,source_max_error=source_error.max(axis=(0,1)).tolist(),
                source_outside_1=int(np.any(source_error>1,axis=-1).sum()),
                source_outside_limits=int((np.any(source_error[...,:3]>1,axis=-1)|(source_error[...,3]>2)).sum()),
                blend_max_error=blend_error.max(axis=(0,1)).tolist(),
                blend_outside_1=int(np.any(blend_error>1,axis=-1).sum())))
        reports.append(dict(test=test,pixels=640*480,max_error=error.max(axis=(0,1)).tolist(),
            outside_1=int(np.any(error>1,axis=-1).sum()),outside_2=int(np.any(error>2,axis=-1).sum()),
            first_expected=wanted[0,0].tolist(),first_actual=actual[0,0].tolist()))
    winding=(results/'composition-probe-4.bin').read_bytes()==(results/'composition-probe-5.bin').read_bytes()
    # The source control preserves observed limits: RGB<=1, unscaled alpha<=2
    # UNORM8 levels. Final RGB is scaled by the captured final color factors;
    # alpha is the unscaled intermediate blue. The blend control is independent
    # of those upstream differences and compares measured shader output.
    return dict(passed=bool(winding and all(r['outside_2']==0 for r in reports) and
                reports[0]['outside_1']==0 and len(controls)==6 and
                all(c['source_outside_limits']==0 and c['blend_outside_1']==0 for c in controls)),
                opposite_winding_identical=winding,precision_controls=controls,
                pixels=8*640*480,fixtures=reports,inputs={f.name:hashlib.sha256(f.read_bytes()).hexdigest()for f in prepared.glob('*.bin')})

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prepared',type=Path,required=True);p.add_argument('--results',type=Path,required=True);p.add_argument('--source-results',type=Path,required=True)
    a=p.parse_args();r=compare(a.prepared,a.results,a.source_results);print(json.dumps(r,indent=2));sys.exit(0 if r['passed'] else 1)
