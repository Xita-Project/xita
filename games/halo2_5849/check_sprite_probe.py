#!/usr/bin/env python3
"""Compare original/synthetic packed-color GPU fixtures; not menu evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import numpy as np


def decode_bc2(data, width, height):
    columns, rows = (width+3)//4, (height+3)//4
    if not width or not height or len(data) != columns*rows*16:
        raise ValueError('incomplete BC2 level')
    image=np.zeros((rows*4,columns*4,4))
    for block in range(columns*rows):
        alpha,e0,e1,indices=struct.unpack_from('<QHHI',data,block*16)
        colors=[np.array([e>>11,(e>>5)&63,e&31])/np.array([31,63,31])for e in (e0,e1)]
        colors += [(2*colors[0]+colors[1])/3,(colors[0]+2*colors[1])/3]
        for pixel in range(16):
            y,x=(block//columns)*4+pixel//4,(block%columns)*4+pixel%4
            image[y,x,:3]=colors[(indices>>(pixel*2))&3]
            image[y,x,3]=((alpha>>(pixel*4))&15)/15
    return image[:height,:width]


def synthetic_blocks(width,height):
    blocks=((width+3)//4)*((height+3)//4)
    data=bytearray(blocks*16)
    for b in range(blocks):
        alpha=sum(((b*3+i)%16)<<(i*4)for i in range(16))
        selectors=sum(((i+b)%4)<<(i*2)for i in range(16))
        struct.pack_into('<QHHI',data,b*16,alpha,(b*197+0xF800)&65535,(b*331+0x07E0)&65535,selectors)
    return data


def sample(image,u,v):
    height,width=image.shape[:2]
    s,t=np.clip(u*width-.5,0,width-1),np.clip(v*height-.5,0,height-1)
    x,y=s.astype(int),t.astype(int)
    f,g=(s-x)[...,None],(t-y)[...,None]
    nx,ny=np.minimum(x+1,width-1),np.minimum(y+1,height-1)
    return ((image[y,x]*(1-f)+image[y,nx]*f)*(1-g)+(image[ny,x]*(1-f)+image[ny,nx]*f)*g)


def transformed(vertices,c):
    """Independent live equations; preserve the original viewport epilogue."""
    v=np.asarray(vertices,dtype=np.float32)
    clip=np.stack([np.sum(v[:,0,:3]*c[i-10,:3],axis=1,dtype=np.float32)+c[i-10,3]for i in range(177,181)],axis=-1)
    if not np.all(clip[:,3]==1):raise ValueError('probe requires original w=1')
    window=clip[:,:3]*c[0,:3]+c[1,:3]
    screen=np.sign(window[:,:2])*np.floor(np.abs(window[:,:2])*16)/16
    uv=(c[183-10,0]*v[:,0,:2]+c[183-10,1]*v[:,1,:2])*c[186-10,:2]
    uv=(uv+c[184-10,2:])*c[181-10,:2]
    return screen.astype(float),uv.astype(float),v[:,2].astype(float)


def fixture(prepared,test):
    width=4 if test>=16 else 8 if test in (12,13)else 1 if test==15 else 1024
    height=4 if test>=16 else 1 if test in (12,15)else 1024 if test==13 else 4 if test==14 else 8
    vertices=np.fromfile(prepared/'sprite.vertices.bin',dtype='<f4').reshape(4,3,4).copy()
    c=np.fromfile(prepared/'sprite.vertex-constants.bin',dtype='<f4').reshape(178,4).copy()
    factors=np.fromfile(prepared/'sprite.constants.bin',dtype='<f4').reshape(18,4).copy()
    if test>=2:vertices[:,0,:2]=[[0,0],[640,0],[640,480],[0,480]]
    if test>=1:vertices[:,2]=1
    if test in (3,4,5):vertices[:,2]=np.array([64,128,192,128])/255
    if test==6:vertices[:,1,:2]+=np.array([.25/width,.25/height],dtype=np.float32)
    if test==7:vertices[:,2,3]=0
    if test==8:
        vertices[:,2]=[[0,0,64/255,128/255],[1,0,64/255,128/255],[1,1,64/255,128/255],[0,1,64/255,128/255]]
        factors[0]=[.25,.5,.75,.8]
    if test==10:c[177-10,3]+=np.float32(.05);c[178-10,3]-=np.float32(1/30)
    if test==11:c[184-10,2]+=np.float32(.125);c[184-10,3]-=np.float32(.25)
    if test>=16:vertices[:,1,:2]=0
    if test==17:vertices[:,2,:3]=0
    if test==18:vertices[:,2]=np.array([64,128,192,128])/255
    if test==19:vertices[:,2,3]=0
    data=(prepared/'sprite.constant.texture.bin').read_bytes()if test>=16 else (prepared/'sprite.texture.bin').read_bytes()if test<2 else synthetic_blocks(width,height)
    return vertices,c,factors,decode_bc2(data,width,height)


def read_rgba(path,width,height):
    raw=path.read_bytes()
    if len(raw)!=width*height*4:raise ValueError('incomplete GPU output')
    words=np.frombuffer(raw,dtype='<u4').reshape(height,width)
    return np.stack([(words>>shift)&255 for shift in (16,8,0,24)],axis=-1).astype(int)


def compare(prepared,results,source_probe=False,point_results=None,measured_sources=None,constant_uv=False):
    y,x=np.indices((480,640))
    destination=np.stack([(x*3+y*17)&255,(x*11+y*13)&255,(x*7+y*5)&255,(x*19+y*23)&255],axis=-1).astype(float)
    reports=[]
    tests=range(16,20)if constant_uv else range(16)
    for test in tests:
        vertices,c,factors,image=fixture(prepared,test)
        if point_results:
            point={0:0,1:0,12:2,13:3,14:4,15:5}.get(test,1)
            image=read_rgba(point_results/f'sprite-point-{point}.bin',image.shape[1],image.shape[0])/255
        screen,uv,colors=transformed(vertices,c)
        if not (np.all(screen[[0,3],0]==screen[0,0])and np.all(screen[[1,2],0]==screen[1,0])and
                np.all(screen[[0,1],1]==screen[0,1])and np.all(screen[[2,3],1]==screen[2,1])):
            raise ValueError('probe rectangle changed shape')
        tx=(x+.5-screen[0,0])/(screen[1,0]-screen[0,0]);ty=(y+.5-screen[0,1])/(screen[3,1]-screen[0,1])
        mask=(tx>=0)&(tx<1)&(ty>=0)&(ty<1)
        if test in (4,9):mask[:]=False
        coords=uv[0]+tx[...,None]*(uv[1]-uv[0])+ty[...,None]*(uv[3]-uv[0])
        color=colors[0]+tx[...,None]*(colors[1]-colors[0])+ty[...,None]*(colors[3]-colors[0])
        stage=np.clip(sample(image,coords[...,0],coords[...,1])*factors[0],-1,1)
        source=np.clip(np.clip(stage*color,-1,1),0,1)
        quantized=np.floor(source*255+.5)
        if measured_sources:
            quantized=read_rgba(measured_sources/f'sprite-probe-source-{test}.bin',640,480).astype(float)
        expected=destination.copy()
        if source_probe:expected[mask]=quantized[mask]
        else:
            alpha=quantized[...,3,None]/255
            rgb=quantized[...,:3]*alpha+destination[...,:3]*(1-alpha)
            expected[mask,:3]=rgb[mask]
        prefix='sprite-probe-source'if source_probe else'sprite-probe'
        actual=read_rgba(results/f'{prefix}-{test}.bin',640,480)
        wanted=np.floor(expected+.5).astype(int);error=np.abs(actual-wanted)
        reports.append(dict(test=test,pixels=640*480,covered=int(mask.sum()),max_error=error.max(axis=(0,1)).tolist(),
            outside_1=int(np.any(error>1,axis=-1).sum()),unchanged_outside=bool(np.array_equal(actual[~mask],destination[~mask])),
            destination_alpha_exact=bool(np.array_equal(actual[...,3],destination[...,3]))if not source_probe else None,
            changed=int(np.any(actual!=destination,axis=-1).sum())))
    prefix='sprite-probe-source'if source_probe else'sprite-probe'
    winding=None if constant_uv else (results/f'{prefix}-3.bin').read_bytes()==(results/f'{prefix}-5.bin').read_bytes()
    return dict(passed=bool((constant_uv or winding) and all(not r['outside_1']and r['unchanged_outside']and
                 (source_probe or r['destination_alpha_exact'])for r in reports)),
        reference='independent original DPH/viewport, normalized BC2, bilinear/projected UV, vertex color, staged clamps and UCHAR4 source blend',
        source_probe=source_probe,constant_uv=constant_uv,pixels=len(tests)*640*480,unculled_reverse_identical=winding,fixtures=reports,
        inputs={f.name:hashlib.sha256(f.read_bytes()).hexdigest()for f in prepared.glob('*.bin')})


def staged_compare(prepared,results,sources,points):
    point_reports=[]
    for test,(width,height)in enumerate([(1024,8),(1024,8),(8,1),(8,1024),(1024,4),(1,1)]):
        data=(prepared/'sprite.texture.bin').read_bytes()if test==0 else synthetic_blocks(width,height)
        expected=np.floor(decode_bc2(data,width,height)*255+.5).astype(int)
        actual=read_rgba(points/f'sprite-point-{test}.bin',width,height)
        error=np.abs(expected-actual)
        point_reports.append(dict(test=test,pixels=width*height,max_error=error.max(axis=(0,1)).tolist(),
            outside_2=int(np.any(error>2,axis=-1).sum()),alpha_exact=bool(np.all(error[...,3]==0))))
    # Keep the failed strict-one ideal comparisons as explicit report fields.
    # Complete point captures establish <=2 decode levels; using those measured
    # texels establishes <=1 sampling/live-stage level. The independently
    # captured source establishes <=1 destination-blend level, with alpha exact.
    ideal=compare(prepared,results)
    ideal_source=compare(prepared,sources,True)
    sampled=compare(prepared,sources,True,point_results=points)
    blended=compare(prepared,results,measured_sources=sources)
    ideal_bounded=all(max(f['max_error'])<=3 and f['unchanged_outside'] for r in (ideal,ideal_source)for f in r['fixtures'])
    original_exact=point_reports[0]['max_error']==[0,0,0,0] and ideal_source['fixtures'][0]['max_error']==[0,0,0,0]
    original_bounded=max(ideal['fixtures'][1]['max_error'])<=1 and ideal['fixtures'][0]['max_error']==[0,0,0,0]
    point_passed=all(not r['outside_2']and r['alpha_exact']for r in point_reports)
    return dict(passed=bool(ideal_bounded and original_exact and original_bounded and point_passed and sampled['passed']and blended['passed']),
        scope='measured GXM/Vita3K precision bounds; no NV2A hardware identity or displayed menu claim',
        point_decode=point_reports,point_pixels=sum(r['pixels']for r in point_reports),
        ideal_full_strict1=ideal,ideal_source_strict1=ideal_source,
        ideal_bounded_3=ideal_bounded,original_decode_exact=original_exact,original_blend_within_1=original_bounded,
        measured_texels_source=sampled,measured_source_blend=blended,
        point_hashes={f.name:hashlib.sha256(f.read_bytes()).hexdigest()for f in points.glob('sprite-point-*.bin')})


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('prepared','results'):ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--source-probe',action='store_true')
    ap.add_argument('--constant-uv',action='store_true')
    ap.add_argument('--point-results',type=Path);ap.add_argument('--source-results',type=Path);a=ap.parse_args()
    if bool(a.point_results)!=bool(a.source_results) or ((a.source_probe or a.constant_uv) and a.point_results):
        ap.error('staged validation requires both point/source results and no source-probe switch')
    report=staged_compare(a.prepared,a.results,a.source_results,a.point_results)if a.point_results else compare(a.prepared,a.results,a.source_probe,constant_uv=a.constant_uv)
    print(json.dumps(report,indent=2));sys.exit(0 if report['passed']else 1)
