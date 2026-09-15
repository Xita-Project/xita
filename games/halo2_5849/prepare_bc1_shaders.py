#!/usr/bin/env python3
"""Prepare private native184 BC1 fixtures; this enables no guest draw."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_screen_shaders import XBE_SHA, PROGRAM_SHA, pixel_definition, vertex_source, pg

SNAPSHOT_SHA = '6b4d7cbcdbc07b6cea821c3fb3b5b81b8857ce1790ac55b49325fbfbb0d97634'
PUSH_SHA = 'f59b53c00623691ad83bc71c7c21767cc542918c8c858f2f65e2f8be9f3b71be'
TEXTURE_SHA = 'fbfbec57446a47e2fb7312b3e4b0f24d73ba094be9ab73e39f954e4c36a4ac61'
COPY_FRAGMENT = '''struct VertOut { float4 position:POSITION; float4 texcoord0:TEXCOORD0; };
float4 main(VertOut IN, uniform sampler2D copy_source):COLOR {
    return tex2Dproj(copy_source, float3(IN.texcoord0.xy, IN.texcoord0.w));
}
'''


def fragment_source(d):
    if (d['warnings'] or d['stage_count'] != 3 or d['same_c0'] or d['same_c1'] or d['mux_msb'] or
        len(d['textures']) != 4 or any(t['mode'] != 'PROJECT2D' or t['dot_mapping'] or
                                     t['compare_mode'] for t in d['textures'])):
        raise ValueError('unsupported BC1 texture/combiner route')
    allowed = {'zero', 'c0', 'c1', 't0', 't1', 't2', 't3', 'r0', 'r1'}
    for st in d['stages']:
        if any(v['reg'] not in allowed for v in st['rgb_in'] + st['alpha_in']):
            raise ValueError('unbound live input')
        if any(st[k][out] not in {'zero', 't0', 't1', 't2', 't3', 'r0', 'r1'}
               for k in ('rgb_out', 'alpha_out') for out in ('ab', 'cd', 'sum')):
            raise ValueError('unsupported combiner output')
    f = d['final']
    if (not f['present'] or f['flags'] or
        any(f[k]['reg'] not in allowed for k in 'abcdefg') or
        any(f[k]['reg'] != 'zero' for k in 'ef')):
        raise ValueError('unsupported final combiner')
    lines = ['''// Four enabled one-level BC1 inputs. Projected coordinates are already
// normalized; repeat/filtering is supplied by the validated sampler state.
struct VertOut { float4 position:POSITION; float4 texcoord0:TEXCOORD0;
    float4 texcoord1:TEXCOORD1; float4 texcoord2:TEXCOORD2; float4 texcoord3:TEXCOORD3; };
float4 main(VertOut IN, uniform sampler2D tex0, uniform sampler2D tex1,
            uniform sampler2D tex2, uniform sampler2D tex3, uniform float4 psc[18]):COLOR {''']
    for i in range(4):
        lines.append(f'    float4 t{i} = tex2Dproj(tex{i}, float3(IN.texcoord{i}.xy, IN.texcoord{i}.w));')
    lines.append('    float4 r0 = float4(0.0,0.0,0.0,t0.a), r1 = float4(0.0,0.0,0.0,0.0);')
    saved = pg.SAME_C[:]
    try:
        pg.SAME_C[:] = [False, False]
        for st in d['stages']:
            pg.emit_stage(st, False, lines, set())
        a, b, c, dd = (pg.src_expr(f[k], None, False) for k in 'abcd')
        g = pg.src_expr(f['g'], None, True)
        lines.append(f'    return saturate(float4({a} * {b} + (1.0 - {a}) * {c} + {dd}, {g}));')
    finally:
        pg.SAME_C[:] = saved
    return '\n'.join(lines + ['}']) + '\n'


def vertices_from_push(raw, header_address):
    if len(raw) < 16:
        raise ValueError('short capture')
    base, size, put, reserved = struct.unpack_from('<4I', raw)
    if reserved or size != len(raw) - 16 or not base <= header_address < put <= base + size:
        raise ValueError('invalid ring bounds')
    cursor = 16 + header_address - base
    def packet(method, count):
        nonlocal cursor
        end = cursor + 4 * (count + 1)
        if end > 16 + put - base or end > len(raw):
            raise ValueError('incomplete quad packet')
        if struct.unpack_from('<I', raw, cursor)[0] != (count << 18) | method:
            raise ValueError('unexpected immediate packet')
        values = struct.unpack_from(f'<{count}I', raw, cursor + 4)
        cursor = end
        return values
    if packet(0x17FC, 1) != (7,):
        raise ValueError('unexpected topology')
    vertices = []
    for _ in range(4):
        regs = [0] * 28  # v5/v6 have no live fragment consumer in this route.
        for reg, method in ((4,0x1A40),(3,0x1A30),(2,0x1A20),(1,0x1A10),(0,0x1518)):
            regs[reg * 4:reg * 4 + 4] = packet(method, 4)
        vertices.extend(regs)
    if packet(0x17FC, 1) != (0,):
        raise ValueError('unexpected quad end')
    return struct.pack('<112I', *vertices)


def prepare(xbe, snapshot, push, texture, out):
    for path, expected in ((xbe,XBE_SHA),(snapshot,SNAPSHOT_SHA),(push,PUSH_SHA),(texture,TEXTURE_SHA)):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError(f'owned input revision mismatch: {path.name}')
    s = json.loads(snapshot.read_text())
    program = struct.pack('<28I', *(w for row in s['program'][:7] for w in row))
    if hashlib.sha256(program).hexdigest() != PROGRAM_SHA:
        raise ValueError('vertex program mismatch')
    vertex, plan = vertex_source(program)
    # Preserve all seven original slots; only this private 320x240 probe's
    # final screen-space conversion differs from the first screen pass.
    for old, new in (('screen.x / 640.0','screen.x / 320.0'),('screen.y / 480.0','screen.y / 240.0')):
        if vertex.count(old) != 1:
            raise ValueError('unexpected screen-space conversion')
        vertex = vertex.replace(old,new)
    d = pixel_definition(s['setup'])
    data = texture.read_bytes()
    if len(data) != 64 or struct.unpack_from('<8I',data) != (1,0x018F1A80,8,8,16,32,0x03310C29,2):
        raise ValueError('unexpected BC1 capture')
    factors = s['setup'][0xA60//4:0xAA0//4] + s['setup'][0x1E20//4:0x1E28//4]
    artifacts = {'bc1.vert.cg':vertex.encode(),'bc1.frag.cg':fragment_source(d).encode(),
        'bc1.copy.frag.cg':COPY_FRAGMENT.encode(),'bc1.texture.bin':data[32:],
        'bc1.vertices.bin':vertices_from_push(push.read_bytes(),s['header_address']),
        'bc1.contract.bin':struct.pack('<II',0x43423148,1)+struct.pack('<2048I',*s['setup'])+
            struct.pack('<64I',*s['setup_valid'])+program+vertices_from_push(push.read_bytes(),s['header_address']),
        'bc1.constants.bin':struct.pack('<72f',*[(v >> k & 255)/255 for v in factors for k in (16,8,0,24)])}
    out.mkdir(parents=True,exist_ok=True)
    for name,data in artifacts.items():
        (out/name).write_bytes(data)
    report = dict(scope='isolated private BC1 probe only; no accepted guest draw',
                  snapshot_sha256=SNAPSHOT_SHA,program_sha256=PROGRAM_SHA,pixel_definition=d,
                  vertex_plan=plan,files={n:hashlib.sha256(v).hexdigest() for n,v in artifacts.items()})
    (out/'bc1-shaders.json').write_text(json.dumps(report,indent=2)+'\n')
    return report['files']

if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','snapshot','push','texture'):
        ap.add_argument(name,type=Path)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args()
    print(json.dumps(prepare(args.xbe,args.snapshot,args.push,args.texture,args.out),indent=2))
