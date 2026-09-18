#!/usr/bin/env python3
"""Prepare the private native193 single-texture screen-blend shader/contract."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from prepare_screen_shaders import XBE_SHA, PROGRAM_SHA, COPY_FRAGMENT, pixel_definition, vertex_source, pg
from prepare_composition_shaders import validate_flow, linear

SNAPSHOT_SHA='4cb09224506c8151fdfdf216d07b53979d032fd35f2133745738110b796ce4f3'
PUSH_SHA='4c308fef527b9988b70be4bd23bedc3924bfb6fa5102eeed2661e78f78a201b7'
TEXTURE_SHA='19fd0888a56f31e71263408a2b7e81896a6e7c1f43c81227da968d9cfb09013a'


def vertices_from_push(raw,address):
    if len(raw)<16:raise ValueError('short ring')
    base,size,put,reserved=struct.unpack_from('<4I',raw)
    if reserved or size!=len(raw)-16 or not base<=address<put<=base+size:raise ValueError('invalid ring bounds')
    cursor=16+address-base
    def packet(method,count):
        nonlocal cursor
        end=cursor+4*(count+1)
        if end>16+put-base or end>len(raw):raise ValueError('incomplete packet')
        if struct.unpack_from('<I',raw,cursor)[0]!=(count<<18)|method:raise ValueError('unexpected packet')
        values=struct.unpack_from(f'<{count}I',raw,cursor+4);cursor=end;return values
    if packet(0x17FC,1)!=(7,):raise ValueError('unexpected topology')
    vertices=[]
    for _ in range(4):
        # v2/v3/v4/v6 feed only unused outputs in this specific fragment route.
        regs=[0]*28
        for reg,method in ((5,0x1A50),(1,0x1A10),(0,0x1518)):regs[reg*4:reg*4+4]=packet(method,4)
        vertices.extend(regs)
    if packet(0x17FC,1)!=(0,):raise ValueError('unexpected END')
    return struct.pack('<112I',*vertices)


def fragment_source(d):
    if (d['warnings'] or d['stage_count']!=1 or d['same_c0'] or d['same_c1'] or d['mux_msb'] or
        [t['mode']for t in d['textures']]!=['PROJECT2D','NONE','NONE','NONE'] or
        any(t['dot_mapping']or t['compare_mode']for t in d['textures'])):
        raise ValueError('unsupported single-texture route')
    f=d['final'];allowed={'zero','c0','c1','t0','r0','r1'}
    if not f['present']or f['flags']or any(f[k]['reg']!='zero'for k in 'ef'):raise ValueError('unsupported final')
    validate_flow(d,texture_units=(0,))
    if any(i['reg']not in allowed for s in d['stages']for i in s['rgb_in']+s['alpha_in']):raise ValueError('unsupported live input')
    if any(s[k][out]not in {'zero','r0','r1'}for s in d['stages']for k in ('rgb_out','alpha_out')for out in ('ab','cd','sum')):raise ValueError('unsupported output')
    lines=['''struct VertOut { float4 position:POSITION; float4 texcoord0:TEXCOORD0; };
float4 main(VertOut IN, uniform sampler2D tex0, uniform float4 psc[18]):COLOR {
    float4 t0=tex2Dproj(tex0,float3(IN.texcoord0.xy/float2(160.0,120.0),IN.texcoord0.w));
    float4 r0=float4(0.0,0.0,0.0,t0.a),r1=float4(0.0,0.0,0.0,0.0);''']
    saved=pg.SAME_C[:]
    try:
        pg.SAME_C[:]=[False,False]
        for stage in d['stages']:pg.emit_stage(stage,False,lines,set())
        a,b,c,dd=(pg.src_expr(f[k],None,False)for k in 'abcd');g=pg.src_expr(f['g'],None,True)
        lines.append(f'    return saturate(float4({a}*{b}+(1.0-{a})*{c}+{dd},{g}));')
    finally:pg.SAME_C[:]=saved
    return '\n'.join(lines+['}'])+'\n'


def prepare(xbe,snapshot,push,texture,out):
    for path,expected in ((xbe,XBE_SHA),(snapshot,SNAPSHOT_SHA),(push,PUSH_SHA),(texture,TEXTURE_SHA)):
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:raise ValueError('requires exact owned native193 capture')
    state=json.loads(snapshot.read_text());program=struct.pack('<28I',*(w for row in state['program'][:7]for w in row))
    if hashlib.sha256(program).hexdigest()!=PROGRAM_SHA:raise ValueError('unexpected vertex program')
    vertex,plan=vertex_source(program);pixel=pixel_definition(state['setup'])
    factors=state['setup'][0xA60//4:0xAA0//4]+state['setup'][0x1E20//4:0x1E28//4]
    files={'blend.vert.cg':vertex.encode(),'blend.frag.cg':fragment_source(pixel).encode(),
        'blend.copy.frag.cg':COPY_FRAGMENT.encode(),'blend.texture.bin':linear(texture,0x02B1B000,160,120,640),
        'blend.vertices.bin':vertices_from_push(push.read_bytes(),state['header_address']),
        'blend.constants.bin':struct.pack('<72f',*[(x>>shift&255)/255 for x in factors for shift in (16,8,0,24)])}
    files['blend.contract.bin']=(struct.pack('<II',0x43473248,1)+struct.pack('<2048I',*state['setup'])+
        struct.pack('<64I',*state['setup_valid'])+program+files['blend.vertices.bin'])
    out.mkdir(parents=True,exist_ok=True)
    for name,b in files.items():(out/name).write_bytes(b)
    report=dict(scope='private original one-sampler screen-blend preparation',vertex_plan=plan,pixel_definition=pixel,
        files={name:hashlib.sha256(b).hexdigest()for name,b in files.items()})
    (out/'blend-shaders.json').write_text(json.dumps(report,indent=2)+'\n');return report['files']

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','snapshot','push','texture'):ap.add_argument(name,type=Path)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    print(json.dumps(prepare(a.xbe,a.snapshot,a.push,a.texture,a.out),indent=2))
