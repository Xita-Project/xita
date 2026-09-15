#!/usr/bin/env python3
"""Prepare the private native194 single-texture luminance strip shader/contract."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from prepare_screen_shaders import XBE_SHA, pixel_definition, pg, vg, decode_function
from dataclasses import asdict
from types import SimpleNamespace
from prepare_composition_shaders import validate_flow, linear

SNAPSHOT_SHA='712dfd93152ba17f11278bcb363c865b645de562e8edd0b6802c4262ff02662c'
PUSH_SHA='40e807a80d890ca403ccdab003306480b9a1d164da11819ae9dcde00aa4a7d49'
TEXTURE_SHA='bb7b316640b79b0989fe89492de92f463a88dd6dbe4d60da0d990a6f9a2b603a'
PROGRAM_SHA='9e4feb6797aeacfa8d64e540369cb3801f9a87287be6e72ab25c1971d20e7f79'


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
    if packet(0x17FC,1)!=(6,):raise ValueError('unexpected topology')
    vertices=[]
    for _ in range(4):
        # v5/v6/v7 feed only unused color/fog outputs in this fragment route.
        regs=[0]*32
        for reg,method in ((1,0x1A10),(2,0x1A20),(3,0x1A30),(4,0x1A40),(0,0x1A00)):regs[reg*4:reg*4+4]=packet(method,4)
        vertices.extend(regs)
    if packet(0x17FC,1)!=(0,):raise ValueError('unexpected END')
    return struct.pack('<128I',*vertices)


def adapt_vertex_source(source):
    assignments=[line for line in source.splitlines()if line.startswith('    OUT.position = ')]
    if len(assignments)!=1 or 'oPos.z * 0.9999'not in assignments[0]:raise ValueError('unexpected position conversion')
    source=source.replace(assignments[0],"""    float2 screen = sign(oPos.xy) * floor(abs(oPos.xy) * 16.0) / 16.0;
    OUT.position = float4((2.0 * screen.x / 640.0 - 1.0) * oPos.w,
                          (1.0 - 2.0 * screen.y / 480.0) * oPos.w,
                          (oPos.z / 16777215.0) * oPos.w, oPos.w);""")
    # The original writes only texture XY. Pinned xemu initializes output W=1;
    # the shared generator's zero W would turn PROJECT2D into division by zero.
    # This correction is local to this exact H2 route and leaves CE unchanged.
    for name in ('oT0','oT1','oT2','oT3'):
        old=name+' = float4(0.0, 0.0, 0.0, 0.0)'
        if source.count(old)!=1:raise ValueError('unexpected texture output initializer')
        source=source.replace(old,name+' = float4(0.0, 0.0, 0.0, 1.0)')
    return source


def vertex_source(program):
    function=asdict(decode_function(struct.pack('<HH',0x2078,12)+program,0))
    if function['warnings']or function['decoded_count']!=12:raise ValueError('unexpected vertex decode')
    declaration={'attributes':[dict(vreg=v,type='FLOAT4',stream=0,offset=v*16,size=16)for v in range(8)]}
    args=SimpleNamespace(mode='function',keep_viewport_epilogue=True,const_count=None)
    source,plan=vg.generate(declaration,function,args,'H2 captured twelve-slot strip program')
    if plan.warnings or plan.skip_slots or plan.c_base+plan.const_bias!=18 or plan.c_count!=8:raise ValueError('unexpected vertex plan')
    details=asdict(plan);details['helpers']=sorted(details['helpers']);details['skip_slots']=sorted(details['skip_slots'])
    return adapt_vertex_source(source),details


def fragment_source(d):
    if (d['warnings'] or d['stage_count']!=1 or d['same_c0'] or d['same_c1'] or
        any(s[k]['mux']for s in d['stages']for k in ('rgb_out','alpha_out')) or
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
    float4 t0=tex2Dproj(tex0,float3(IN.texcoord0.xy/float2(640.0,480.0),IN.texcoord0.w));
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
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected:raise ValueError('requires exact owned native194 capture')
    state=json.loads(snapshot.read_text());program=struct.pack('<48I',*(w for row in state['program'][:12]for w in row))
    if hashlib.sha256(program).hexdigest()!=PROGRAM_SHA:raise ValueError('unexpected vertex program')
    vertex,plan=vertex_source(program);pixel=pixel_definition(state['setup'])
    factors=state['setup'][0xA60//4:0xAA0//4]+state['setup'][0x1E20//4:0x1E28//4]
    files={'luma.vert.cg':vertex.encode(),'luma.frag.cg':fragment_source(pixel).encode(),
        'luma.texture.bin':linear(texture,0x038E8000,640,480,2560),
        'luma.vertices.bin':vertices_from_push(push.read_bytes(),state['header_address']),
        'luma.vertex-constants.bin':struct.pack('<32I',*(v for row in state['constants'][18:26]for v in row)),
        'luma.constants.bin':struct.pack('<72f',*[(x>>shift&255)/255 for x in factors for shift in (16,8,0,24)])}
    files['luma.contract.bin']=(struct.pack('<II',0x43553248,1)+struct.pack('<2048I',*state['setup'])+
        struct.pack('<64I',*state['setup_valid'])+program+files['luma.vertex-constants.bin']+files['luma.vertices.bin'])
    out.mkdir(parents=True,exist_ok=True)
    for name,b in files.items():(out/name).write_bytes(b)
    report=dict(scope='private original one-sampler luminance strip preparation',vertex_plan=plan,pixel_definition=pixel,
        files={name:hashlib.sha256(b).hexdigest()for name,b in files.items()})
    (out/'luma-shaders.json').write_text(json.dumps(report,indent=2)+'\n');return report['files']

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','snapshot','push','texture'):ap.add_argument(name,type=Path)
    ap.add_argument('--out',type=Path,required=True);a=ap.parse_args()
    print(json.dumps(prepare(a.xbe,a.snapshot,a.push,a.texture,a.out),indent=2))
