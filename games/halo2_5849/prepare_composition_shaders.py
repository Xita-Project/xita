#!/usr/bin/env python3
"""Prepare the private native186 composition pass; no guest draw is enabled."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_screen_shaders import XBE_SHA, PROGRAM_SHA, COPY_FRAGMENT, pixel_definition, vertex_source, pg
from prepare_bc1_shaders import vertices_from_push

SNAPSHOT_SHA='01c41cab9b4892c0831c5ff35f8682a3d991ece0ebba78f049cabf01f3196fef'
PUSH_SHA='6ee03a2355bda9dc878ca2412efb320dc849e945e90f4bcabb86c0f3e9f69498'
TEXTURE3_SHA='66a31947712af4ad13173d1b37f8570dd08c439b6ce532b1b92021d44e1c09a7'


def validate_flow(d):
    """Reject initial vertex colors/temporary components that have no producer.

    Stage input operands see the state before its parallel result assignments.
    Mask bits0..2 are RGB, bit3 alpha. No unavailable t1 color is substituted.
    """
    known={'zero':15,'c0':15,'c1':15,'t0':15,'t2':15,'t3':15,'r0':8,'r1':0,'v0':0,'v1':0}
    def read(inp,alpha):
        mask=(8 if inp['channel']=='alpha' else 4) if alpha else (8 if inp['channel']=='alpha_rep' else 7)
        if (known.get(inp['reg'],0)&mask) != mask:
            raise ValueError(f"unproduced {inp['reg']} component")
    writable={'t0','t2','t3','r0','r1','v0','v1'}
    for stage in d['stages']:
        for inp in stage['rgb_in']:read(inp,False)
        for inp in stage['alpha_in']:read(inp,True)
        updates={}
        for part,mask in (('rgb_out',7),('alpha_out',8)):
            out=stage[part]
            for slot in ('ab','cd','sum'):
                reg=out[slot]
                if reg=='zero':continue
                if reg not in writable:raise ValueError('unsupported output')
                updates[reg]=updates.get(reg,0)|mask
            if part=='rgb_out':
                for slot in ('ab','cd'):
                    if out[slot+'_blue_to_alpha'] and out[slot]!='zero':
                        updates[out[slot]]=updates.get(out[slot],0)|8
        for reg,mask in updates.items():known[reg]|=mask
    for slot in 'abcdef':read(d['final'][slot],False)
    read(d['final']['g'],True)
    return known


def fragment_source(d):
    modes=[(t['mode'],t['dot_mapping'])for t in d['textures']]
    if (d['warnings'] or d['stage_count']!=6 or d['same_c0'] or d['same_c1'] or d['mux_msb'] or
        modes!=[('PROJECT2D',0),('DOTPRODUCT',4),('DOT_ST',4),('PROJECT2D',0)] or
        d['textures'][1]['input_stage'] is not None or d['textures'][2]['input_stage']!=0 or
        any(t['compare_mode']for t in d['textures'])):
        raise ValueError('unsupported composition route')
    f=d['final']
    if not f['present'] or f['flags'] or any(f[k]['reg']!='zero' for k in 'ef'):
        raise ValueError('unsupported final combiner')
    validate_flow(d)
    lines=['''// Original component producers are checked before emission. Initial v0/v1
// values below only initialize dead components; no vertex color is invented.
struct VertOut { float4 position:POSITION; float4 texcoord0:TEXCOORD0;
    float4 texcoord1:TEXCOORD1; float4 texcoord2:TEXCOORD2; float4 texcoord3:TEXCOORD3; };
float4 main(VertOut IN, uniform sampler2D tex0, uniform sampler2D tex2,
            uniform sampler2D tex3, uniform float4 psc[18]):COLOR {
    float4 t0=tex2Dproj(tex0,float3(IN.texcoord0.xy/float2(640.0,480.0),IN.texcoord0.w));
    float4 bytes0=floor(saturate(t0)*255.0+0.5);
    float3 hilo=float3(dot(bytes0.ar,float2(256.0,1.0))/65535.0,
                      dot(bytes0.gb,float2(256.0,1.0))/65535.0,1.0);
    float dot1=dot(IN.texcoord1.xyz,hilo),dot2=dot(IN.texcoord2.xyz,hilo);
    float4 t2=tex2D(tex2,float2(dot1,dot2));
    float4 t3=tex2Dproj(tex3,float3(IN.texcoord3.xy/float2(320.0,240.0),IN.texcoord3.w));
    float4 r0=float4(0.0,0.0,0.0,t0.a),r1=float4(0.0,0.0,0.0,0.0);
    float4 v0=float4(0.0,0.0,0.0,0.0),v1=float4(0.0,0.0,0.0,0.0);''']
    saved=pg.SAME_C[:]
    try:
        pg.SAME_C[:]=[False,False]
        for st in d['stages']:pg.emit_stage(st,False,lines,set())
        a,b,c,dd=(pg.src_expr(f[k],None,False)for k in 'abcd');g=pg.src_expr(f['g'],None,True)
        lines.append(f'    return saturate(float4({a}*{b}+(1.0-{a})*{c}+{dd},{g}));')
    finally:pg.SAME_C[:]=saved
    return '\n'.join(lines+['}'])+'\n'


def linear(path,physical,width,height,pitch):
    data=path.read_bytes()
    if len(data)!=32+pitch*height or struct.unpack_from('<8I',data)!=(1,physical,width,height,pitch,pitch*height,0x11229,0):
        raise ValueError('unexpected complete ARGB capture')
    return data[32:]


def prepare(xbe,snapshot,push,texture0,texture2,texture3,out):
    for path,expected in ((xbe,XBE_SHA),(snapshot,SNAPSHOT_SHA),(push,PUSH_SHA),(texture3,TEXTURE3_SHA)):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:raise ValueError('owned capture revision mismatch')
    state=json.loads(snapshot.read_text());program=struct.pack('<28I',*(w for row in state['program'][:7]for w in row))
    if hashlib.sha256(program).hexdigest()!=PROGRAM_SHA:raise ValueError('program mismatch')
    vertex,plan=vertex_source(program);pixel=pixel_definition(state['setup'])
    blocks=texture2.read_bytes()
    if len(blocks)!=96 or struct.unpack_from('<8I',blocks)!=(1,0x018FA680,8,8,32,64,0x03310E29,1):
        raise ValueError('unexpected complete BC2 capture')
    factors=state['setup'][0xA60//4:0xAA0//4]+state['setup'][0x1E20//4:0x1E28//4]
    artifacts={'composition.vert.cg':vertex.encode(),'composition.frag.cg':fragment_source(pixel).encode(),
        'composition.copy.frag.cg':COPY_FRAGMENT.encode(),
        'composition.texture0.bin':linear(texture0,0x037BC000,640,480,2560),
        'composition.texture2.bin':blocks[32:],'composition.texture3.bin':linear(texture3,0x02B48000,320,240,1280),
        'composition.vertices.bin':vertices_from_push(push.read_bytes(),state['header_address']),
        'composition.constants.bin':struct.pack('<72f',*[(v>>shift&255)/255 for v in factors for shift in (16,8,0,24)])}
    out.mkdir(parents=True,exist_ok=True)
    for name,data in artifacts.items():(out/name).write_bytes(data)
    report=dict(scope='private composition probe only; no guest draw accepted',vertex_plan=plan,
                pixel_definition=pixel,component_producers=validate_flow(pixel),
                files={n:hashlib.sha256(v).hexdigest()for n,v in artifacts.items()})
    (out/'composition-shaders.json').write_text(json.dumps(report,indent=2)+'\n');return report['files']

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','snapshot','push','texture0','texture2','texture3'):ap.add_argument(name,type=Path)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    print(json.dumps(prepare(a.xbe,a.snapshot,a.push,a.texture0,a.texture2,a.texture3,a.out),indent=2))
