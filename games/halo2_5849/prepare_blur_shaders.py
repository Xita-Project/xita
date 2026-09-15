#!/usr/bin/env python3
"""Prepare the private native191 four-sample blur shaders and command contract."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from prepare_screen_shaders import XBE_SHA,PROGRAM_SHA,pixel_definition,pg
from prepare_composition_shaders import validate_flow,linear
from prepare_threshold_shaders import vertices_from_push, threshold_vertex

SNAPSHOT_SHA='ab4ae308a1c527b6cf20646181585849a11cd59aa869f04761040eefc1d8140a'
PUSH_SHA='af216587e0020f4adcd473c83836cbda855ddadf512e44bc3b0d24263c6372ac'
TEXTURE_SHA='19fd0888a56f31e71263408a2b7e81896a6e7c1f43c81227da968d9cfb09013a'


def fragment_source(d):
    if (d['warnings'] or d['stage_count']!=3 or d['same_c0'] or d['same_c1'] or d['mux_msb'] or
        any(t['mode']!='PROJECT2D' or t['dot_mapping'] or t['compare_mode'] for t in d['textures'])):
        raise ValueError('unsupported blur texture/combiner route')
    f=d['final']
    if not f['present'] or f['flags']:raise ValueError('unsupported final combiner')
    validate_flow(d,texture_units=(0,1,2,3),final_product=True)
    # Only sampled colors and r0/r1 are live; the original extra v5 attribute
    # feeds an unused vertex color output. Preserve the actual vertex words.
    allowed={'zero','c0','c1','t0','t1','t2','t3','r0','r1'}
    if any(i['reg'] not in allowed for s in d['stages'] for i in s['rgb_in']+s['alpha_in']):
        raise ValueError('unsupported live blur input')
    if any(s[k][out] not in {'zero','r0','r1'} for s in d['stages'] for k in ('rgb_out','alpha_out') for out in ('ab','cd','sum')):
        raise ValueError('unsupported blur destination')
    lines=['''struct VertOut { float4 position:POSITION; float4 texcoord0:TEXCOORD0;
    float4 texcoord1:TEXCOORD1; float4 texcoord2:TEXCOORD2; float4 texcoord3:TEXCOORD3; };
float4 main(VertOut IN, uniform sampler2D tex0, uniform sampler2D tex1,
    uniform sampler2D tex2, uniform sampler2D tex3, uniform float4 psc[18]):COLOR {
    float4 t0=tex2Dproj(tex0,float3(IN.texcoord0.xy/float2(160.0,120.0),IN.texcoord0.w));
    float4 t1=tex2Dproj(tex1,float3(IN.texcoord1.xy/float2(160.0,120.0),IN.texcoord1.w));
    float4 t2=tex2Dproj(tex2,float3(IN.texcoord2.xy/float2(160.0,120.0),IN.texcoord2.w));
    float4 t3=tex2Dproj(tex3,float3(IN.texcoord3.xy/float2(160.0,120.0),IN.texcoord3.w));
    float4 r0=float4(0.0,0.0,0.0,t0.a),r1=float4(0.0,0.0,0.0,0.0);''']
    saved=pg.SAME_C[:]
    try:
        pg.SAME_C[:]=[False,False]
        for s in d['stages']:pg.emit_stage(s,False,lines,set())
        e,ff=(pg.src_expr(f[k],None,False)for k in 'ef')
        lines.append(f'    float4 ef_prod=float4({e}*{ff},0.0);')
        a,b,c,dd=(pg.src_expr(f[k],None,False)for k in 'abcd');g=pg.src_expr(f['g'],None,True)
        lines.append(f'    return saturate(float4({a}*{b}+(1.0-{a})*{c}+{dd},{g}));')
    finally:pg.SAME_C[:]=saved
    return '\n'.join(lines+['}'])+'\n'


def prepare(xbe,snapshot,push,texture,out):
    for path,expected in ((xbe,XBE_SHA),(snapshot,SNAPSHOT_SHA),(push,PUSH_SHA),(texture,TEXTURE_SHA)):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:raise ValueError('requires exact owned native191 capture')
    state=json.loads(snapshot.read_text());program=struct.pack('<28I',*(w for row in state['program'][:7]for w in row))
    if hashlib.sha256(program).hexdigest()!=PROGRAM_SHA:raise ValueError('unexpected original program')
    vertex,plan=threshold_vertex(program);pixel=pixel_definition(state['setup'])
    factors=state['setup'][0xA60//4:0xAA0//4]+state['setup'][0x1E20//4:0x1E28//4]
    data={'blur.vert.cg':vertex.encode(),'blur.frag.cg':fragment_source(pixel).encode(),
        'blur.texture.bin':linear(texture,0x02B1B000,160,120,640),
        'blur.vertices.bin':vertices_from_push(push.read_bytes(),state['header_address']),
        'blur.copy.frag.cg':b'''struct VertOut { float4 position:POSITION; float4 texcoord0:TEXCOORD0; };
float4 main(VertOut IN, uniform sampler2D copy_source):COLOR {
 return tex2Dproj(copy_source,float3(IN.texcoord0.xy/float2(160.0,120.0),IN.texcoord0.w));
}
''',
        'blur.constants.bin':struct.pack('<72f',*[(x>>shift&255)/255 for x in factors for shift in (16,8,0,24)])}
    data['blur.contract.bin']=(struct.pack('<II',0x434C3248,1)+
        struct.pack('<2048I',*state['setup'])+struct.pack('<64I',*state['setup_valid'])+
        program+data['blur.vertices.bin'])
    out.mkdir(parents=True,exist_ok=True)
    for name,b in data.items():(out/name).write_bytes(b)
    report=dict(scope='private blur shader and exact command contract',vertex_plan=plan,
                pixel_definition=pixel,files={name:hashlib.sha256(b).hexdigest()for name,b in data.items()})
    (out/'blur-shaders.json').write_text(json.dumps(report,indent=2)+'\n');return report['files']

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','snapshot','push','texture'):ap.add_argument(name,type=Path)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    print(json.dumps(prepare(a.xbe,a.snapshot,a.push,a.texture,a.out),indent=2))
