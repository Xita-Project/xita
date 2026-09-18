#!/usr/bin/env python3
"""Independent BC1/repeat/linear/combiner oracle for eight private GPU fixtures.

Requires NumPy. This compares the observed route, not general NV2A behavior.
No guest draw is accepted by the probe; its pixels are not a displayed menu.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import numpy as np


def decode(data):
    if len(data) != 32:
        raise ValueError('requires four BC1 blocks')
    image = np.zeros((8,8,4))
    for block in range(4):
        a,b,indices=struct.unpack_from('<HHI',data,block*8)
        rgb=[np.array([v>>11,(v>>5)&63,v&31])/np.array([31,63,31]) for v in (a,b)]
        if a>b:
            rgb.extend([(2*rgb[0]+rgb[1])/3,(rgb[0]+2*rgb[1])/3])
            alpha=[1,1,1,1]
        else:
            rgb.extend([(rgb[0]+rgb[1])/2,np.zeros(3)])
            alpha=[1,1,1,0]
        for pixel in range(16):
            k=(indices>>(pixel*2))&3;y,x=(block//2)*4+pixel//4,(block%2)*4+pixel%4
            image[y,x,:3]=rgb[k];image[y,x,3]=alpha[k]
    return image


def sample(image,u,v):
    s,t=u*8-.5,v*8-.5
    x,y=np.floor(s).astype(int),np.floor(t).astype(int)
    f,g=(s-x)[...,None],(t-y)[...,None]
    return ((image[y%8,x%8]*(1-f)+image[y%8,(x+1)%8]*f)*(1-g)+
            (image[(y+1)%8,x%8]*(1-f)+image[(y+1)%8,(x+1)%8]*f)*g)


def synthetic(unit=0):
    colors=[0xF800,0x07E0,0x001F,0xFFFF]
    return b''.join(struct.pack('<HHI',colors[(block+unit)%4],colors[3-(block+unit)%4],0xE4E4E4E4)
                    for block in range(4))


def compare(prepared,results,sample_results=None,encoding=None):
    original_c=np.fromfile(prepared/'bc1.constants.bin',dtype='<f4').reshape(18,4)
    vertices=np.fromfile(prepared/'bc1.vertices.bin',dtype='<f4').reshape(4,7,4)
    blocks=(prepared/'bc1.texture.bin').read_bytes()
    if not np.all(np.isfinite(original_c)) or not np.all(np.isfinite(vertices)):
        raise ValueError('nonfinite input')
    if not np.array_equal(vertices[:,0,:2],[[0,0],[0,240],[320,240],[320,0]]):
        raise ValueError('unsupported geometry')
    y,x=np.indices((240,320));px=(x+.5)/320;py=(y+.5)/240
    reports=[]
    for test in range(8):
        c=original_c.copy()
        if test in (6,7):
            c=(((np.arange(72)*73+41)&255)/255).astype(np.float32).reshape(18,4)
        tex=[]
        for unit in range(4):
            image=decode(blocks if test in (0,5) else synthetic(unit if test==7 else 0))
            if test>=2:
                u=-1.125+unit*.125+px*3.5
                v=-.75-unit*.25+py*2.5
            else:
                corners=vertices[:,unit+1].astype(float)
                if not np.all(corners[:,3]==1):
                    raise ValueError('only captured projective divisor1 is audited')
                u=corners[0,0]+px*(corners[3,0]-corners[0,0])+py*(corners[1,0]-corners[0,0])
                v=corners[0,1]+px*(corners[3,1]-corners[0,1])+py*(corners[1,1]-corners[0,1])
            tex.append(sample(image,u,v))
        if test in (4,5):
            expected=decode(synthetic() if test==4 else blocks)[y//30,x//40]*255
        else:
            dot=[np.clip(np.sum(tex[u][...,:3]*c[index,:3],axis=-1),-1,1)
                 for u,index in enumerate((0,8,1,9))]
            rgb=np.clip(dot[1][...,None]*c[2,:3]+dot[2][...,None]*c[10,:3],-1,1)
            rgb=np.clip(rgb+dot[3][...,None]*c[16,:3],0,1)
            expected=np.concatenate([rgb,np.clip(dot[0][...,None],0,1)],axis=-1)*255
        wanted=np.floor(expected+.5).astype(int)
        raw=(results/f'bc1-probe-{test}.bin').read_bytes()
        if len(raw)!=320*240*4:
            raise ValueError('incomplete GPU output')
        words=np.frombuffer(raw,dtype='<u4').reshape(240,320)
        actual=np.stack([(words>>shift)&255 for shift in (16,8,0,24)],axis=-1).astype(int)
        error=np.abs(wanted-actual)
        reports.append(dict(test=test,pixels=320*240,max_error=error.max(axis=(0,1)).tolist(),
            outside_1=int(np.any(error>1,axis=-1).sum()),outside_2=int(np.any(error>2,axis=-1).sum()),mean_error=error.mean(axis=(0,1)).tolist(),
            first_expected=wanted[0,0].tolist(),first_actual=actual[0,0].tolist()))
    winding=(results/'bc1-probe-2.bin').read_bytes()==(results/'bc1-probe-3.bin').read_bytes()
    isolation = None
    if sample_results is not None:
        isolation = isolate(prepared, results, sample_results, encoding)
    # Preserve the original strict comparison counts. The independent point,
    # sampler and combiner controls explain the compound two-level bound; it
    # is not a claim of bit-exact NV2A texel/filter precision.
    return dict(passed=bool(winding and all(r['outside_2']==0 for r in reports) and
                            isolation is not None and isolation['passed']),
                precision_isolation=isolation,
                opposite_winding_identical=winding,pixels=8*320*240,fixtures=reports,
                inputs={n:hashlib.sha256((prepared/n).read_bytes()).hexdigest() for n in
                        ('bc1.constants.bin','bc1.vertices.bin','bc1.texture.bin')})

def isolate(prepared,results,sample_results,encoding):
    if encoding not in ('half','vita3k-unorm16'):
        raise ValueError('explicit RGBA16 readback encoding required')
    def capture(test,unit):
        data=(sample_results/f'bc1-probe-sample-{test}-{unit}.rgba16').read_bytes()
        if len(data)!=320*240*8:
            raise ValueError('incomplete sampler output')
        a=np.frombuffer(data,dtype='<u2' if encoding=='vita3k-unorm16' else '<f2').astype(float).reshape(240,320,4)
        if encoding=='vita3k-unorm16':
            a/=65535
        if not np.all(np.isfinite(a)) or np.any(a<0) or np.any(a>1):
            raise ValueError('invalid normalized sampler capture')
        return a
    blocks=(prepared/'bc1.texture.bin').read_bytes()
    original_c=np.fromfile(prepared/'bc1.constants.bin',dtype='<f4').reshape(18,4)
    vertices=np.fromfile(prepared/'bc1.vertices.bin',dtype='<f4').reshape(4,7,4)
    cap=capture(5,0)[15::30,20::40]
    syn=capture(4,0)[15::30,20::40]
    codec=[]
    for label,actual,data in (('captured',cap,blocks),('synthetic',syn,synthetic())):
        error=np.abs(np.floor(decode(data)*255+.5)-np.floor(actual*255+.5))
        codec.append(dict(kind=label,max_error=error.max(axis=(0,1)).tolist(),outside_1=int(np.any(error>1,axis=-1).sum())))
    y,x=np.indices((240,320));px=(x+.5)/320;py=(y+.5)/240
    samplers=[];combiners=[]
    for test in range(8):
        c=original_c.copy()
        if test in (6,7):c=(((np.arange(72)*73+41)&255)/255).astype(np.float32).reshape(18,4)
        tex=[]
        for unit in range(4):
            actual=capture(test,unit);tex.append(actual)
            image=(cap if test in (0,5) else syn).copy()
            if test==7:
                for block in range(4):
                    k=(block+unit)%4
                    image[block//2*4:block//2*4+4,block%2*4:block%2*4+4]=syn[k//2*4:k//2*4+4,k%2*4:k%2*4+4]
            if test>=2:u=-1.125+unit*.125+px*3.5;v=-.75-unit*.25+py*2.5
            else:
                co=vertices[:,unit+1].astype(float)
                u=co[0,0]+px*(co[3,0]-co[0,0])+py*(co[1,0]-co[0,0])
                v=co[0,1]+px*(co[3,1]-co[0,1])+py*(co[1,1]-co[0,1])
            expected=image[y//30,x//40] if test in (4,5) else sample(image,u,v)
            error=np.abs(expected-actual)*255
            samplers.append(dict(test=test,unit=unit,max_error=error.max(axis=(0,1)).tolist(),outside_2=int(np.any(error>2,axis=-1).sum())))
        dot=[np.clip(np.sum(tex[u][...,:3]*c[i,:3],axis=-1),-1,1) for u,i in enumerate((0,8,1,9))]
        rgb=np.clip(dot[1][...,None]*c[2,:3]+dot[2][...,None]*c[10,:3],-1,1)
        rgb=np.clip(rgb+dot[3][...,None]*c[16,:3],0,1)
        expected=np.concatenate([rgb,np.clip(dot[0][...,None],0,1)],axis=-1)
        if test in (4,5):expected=tex[0]
        words=np.fromfile(results/f'bc1-probe-{test}.bin',dtype='<u4').reshape(240,320)
        actual=np.stack([(words>>s)&255 for s in (16,8,0,24)],axis=-1)
        error=np.abs(np.floor(expected*255+.5).astype(int)-actual)
        combiners.append(dict(test=test,max_error=error.max(axis=(0,1)).tolist(),outside_1=int(np.any(error>1,axis=-1).sum())))
    return dict(passed=all(r['outside_1']==0 for r in codec+combiners) and all(r['outside_2']==0 for r in samplers),
                encoding=encoding,codec=codec,samplers=samplers,combiners=combiners,
                note='Point decoding, then ideal linear/repeat from measured decoded texels, then CPU combiner equations from measured samples; not independent full hardware equivalence')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prepared',type=Path,required=True);p.add_argument('--results',type=Path,required=True)
    p.add_argument('--sample-results',type=Path,required=True)
    p.add_argument('--sample-encoding',choices=('half','vita3k-unorm16'),required=True)
    a=p.parse_args();report=compare(a.prepared,a.results,a.sample_results,a.sample_encoding)
    print(json.dumps(report,indent=2));sys.exit(0 if report['passed'] else 1)
