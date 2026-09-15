#!/usr/bin/env python3
"""Prepare the owned native196 packed-color quad for isolated GPU validation."""
import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import struct
from types import SimpleNamespace
from prepare_screen_shaders import XBE_SHA, COPY_FRAGMENT, pixel_definition, vg, decode_function

SNAPSHOT_SHA = '0f4b86d50e00eca9206a05c73231125fc2736679582a835fc75560ffb9dc5e52'
PUSH_SHA = '1d647e868e676552b5e4c0539d4d527cf2abce9f1cd65ded98d197027b1b051f'
TEXTURE_SHA = 'd4d48eaf9037a98054ff42ea3542cbcb6139ef477b016927dd8cfa8862c2684b'
PROGRAM_SHA = '1066fb4c648f1147a62e4fe4c34dbe30ee40c89f0d24e6399c13ca6fc8fa622c'


def unpack_vertices(raw):
    if len(raw) != 80:
        raise ValueError('requires four float2/float2/UB_D3D vertices')
    words = struct.unpack('<20I', raw)
    expanded = bytearray()
    for offset in range(0, 20, 5):
        # Exact original coordinate bits, defined absent z=0/w=1. D3D color
        # bytes are B,G,R,A in little-endian memory, normalized independently.
        floats = struct.unpack('<4f', struct.pack('<4I', *words[offset:offset+4]))
        if any(not (-65536 <= value <= 65536) for value in floats):
            raise ValueError('unbounded/nonfinite quad coordinate')
        expanded += struct.pack('<4I', *words[offset:offset+2], 0, 0x3F800000)
        expanded += struct.pack('<4I', *words[offset+2:offset+4], 0, 0x3F800000)
        color = words[offset+4]
        expanded += struct.pack('<4f', *[((color >> shift) & 255) / 255 for shift in (16, 8, 0, 24)])
    return bytes(expanded)


def vertices_from_push(raw, address):
    if len(raw) < 16:
        raise ValueError('short ring')
    base, size, put, reserved = struct.unpack_from('<4I', raw)
    if reserved or size != len(raw)-16 or not base <= address < put <= base+size:
        raise ValueError('invalid ring bounds')
    cursor = 16+address-base
    def packet(method, count, non_increasing=False):
        nonlocal cursor
        end = cursor + 4*(count+1)
        if end > min(len(raw), 16+put-base):
            raise ValueError('incomplete packet')
        expected = (count << 18) | method | (0x40000000 if non_increasing else 0)
        if struct.unpack_from('<I', raw, cursor)[0] != expected:
            raise ValueError('unexpected packet')
        data = raw[cursor+4:end]
        cursor = end
        return data
    if packet(0x17FC, 1) != struct.pack('<I', 8):
        raise ValueError('requires original quad topology')
    vertices = packet(0x1818, 20, True)
    if packet(0x17FC, 1) != bytes(4):
        raise ValueError('unexpected END')
    unpack_vertices(vertices)
    return vertices


def adapt_vertex_source(source):
    assignments = [line for line in source.splitlines() if line.startswith('    OUT.position = ')]
    if len(assignments) != 1 or 'oPos.z * 0.9999' not in assignments[0]:
        raise ValueError('unexpected position conversion')
    source = source.replace(assignments[0], '''    float2 screen = sign(oPos.xy) * floor(abs(oPos.xy) * 16.0) / 16.0;
    OUT.position = float4((2.0 * screen.x / 640.0 - 1.0) * oPos.w,
                          (1.0 - 2.0 * screen.y / 480.0) * oPos.w,
                          (oPos.z / 16777215.0) * oPos.w, oPos.w);''')
    for name in ('oT0', 'oT1', 'oT2'):
        old = name+' = float4(0.0, 0.0, 0.0, 0.0)'
        if source.count(old) != 1:
            raise ValueError('unexpected texture-output initializer')
        source = source.replace(old, name+' = float4(0.0, 0.0, 0.0, 1.0)')
    return source


def vertex_source(program):
    function = asdict(decode_function(struct.pack('<HH', 0x2078, 21)+program, 0))
    if function['warnings'] or function['decoded_count'] != 21:
        raise ValueError('unexpected vertex decode')
    declaration = {'attributes': [dict(vreg=v, type='FLOAT4', stream=0, offset=v*16, size=16) for v in range(3)]}
    source, plan = vg.generate(declaration, function,
        SimpleNamespace(mode='function', keep_viewport_epilogue=True, const_count=None),
        'H2 captured 21-slot packed-color quad')
    if plan.warnings or plan.skip_slots or plan.c_base+plan.const_bias != 10 or plan.c_count != 178:
        raise ValueError('unexpected vertex plan')
    details = asdict(plan)
    details['helpers'] = sorted(details['helpers'])
    details['skip_slots'] = sorted(details['skip_slots'])
    return adapt_vertex_source(source), details


def fragment_source(d):
    # This exact two-stage definition writes t1/t2, but neither value reaches
    # final r0. Validate those dead branches explicitly rather than fabricating
    # samples for NONE stages or suppressing arbitrary decoder warnings.
    if (d['warnings'] != ['t1 is read but texture stage 1 mode is NONE',
                          't2 is read but texture stage 2 mode is NONE'] or
        d['stage_count'] != 2 or len(d['stages']) != 2 or d['same_c0'] or d['same_c1'] or
        [t['mode'] for t in d['textures']] != ['PROJECT2D','NONE','NONE','NONE'] or
        any(t['compare_mode'] or t['dot_mapping'] for t in d['textures'])):
        raise ValueError('unsupported packed-color texture route')
    for number, stage in enumerate(d['stages']):
        inputs = ('t0','c0','t1','c1') if number == 0 else ('t2','c0','t0','v0')
        outputs = ('t0','t1','zero') if number == 0 else ('t2','r0','zero')
        for kind, channel in (('rgb', 'rgb'), ('alpha', 'alpha')):
            if len(stage[kind+'_in']) != 4 or any((value['reg'], value['channel'], value['mapping']) != (register, channel, 'unsigned_identity')
                   for value, register in zip(stage[kind+'_in'], inputs)):
                raise ValueError('unsupported live or dead stage input')
            o = stage[kind+'_out']
            if (tuple(o[name] for name in ('ab','cd','sum')) != outputs or
                o['scale'] != 'identity' or any(o[k] for k in
                    ('mux','ab_dot','cd_dot','ab_blue_to_alpha','cd_blue_to_alpha'))):
                raise ValueError('unsupported stage output/dependency')
    f = d['final']
    if not f['present'] or f['flags']:
        raise ValueError('unsupported final control')
    for name in 'abcdefg':
        expected = ('r0' if name in 'dg' else 'zero', 'alpha' if name == 'g' else 'rgb', 'unsigned_identity')
        if (f[name]['reg'], f[name]['channel'], f[name]['mapping']) != expected:
            raise ValueError('unsupported final dependency')
    return '''// Exact live slice: stage0 AB -> t0, stage1 CD -> r0, final r0.
// Dead t1/t2 products cannot reach any output; no unbound samples are invented.
struct VertOut { float4 position:POSITION; float4 color0:COLOR0; float4 texcoord0:TEXCOORD0; };
float4 main(VertOut IN, uniform sampler2D tex0, uniform float4 psc[18]):COLOR {
    float4 t0 = clamp(tex2Dproj(tex0, float3(IN.texcoord0.xy, IN.texcoord0.w)) * psc[0], -1.0, 1.0);
    float4 r0 = clamp(t0 * IN.color0, -1.0, 1.0);
    return saturate(r0);
}
'''


def prepare(xbe, snapshot, push, texture, out):
    for path, expected in ((xbe,XBE_SHA),(snapshot,SNAPSHOT_SHA),(push,PUSH_SHA),(texture,TEXTURE_SHA)):
        if hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError('requires exact owned native196 capture')
    state = json.loads(snapshot.read_text())
    program = struct.pack('<84I', *(w for row in state['program'][:21] for w in row))
    if hashlib.sha256(program).hexdigest() != PROGRAM_SHA:
        raise ValueError('unexpected vertex program')
    header = struct.unpack_from('<8I', texture.read_bytes())
    if header != (1,0x2C7000,1024,8,4096,8192,0x03A10E29,1) or texture.stat().st_size != 8224:
        raise ValueError('unexpected BC2 capture shape')
    vertex, plan = vertex_source(program)
    pixel = pixel_definition(state['setup'])
    raw_vertices = vertices_from_push(push.read_bytes(),state['header_address'])
    factors = state['setup'][0xA60//4:0xAA0//4]+state['setup'][0x1E20//4:0x1E28//4]
    files = {'sprite.vert.cg':vertex.encode(), 'sprite.frag.cg':fragment_source(pixel).encode(),
        'sprite.copy.frag.cg':COPY_FRAGMENT.encode(), 'sprite.texture.bin':texture.read_bytes()[32:],
        'sprite.inline.bin':raw_vertices, 'sprite.vertices.bin':unpack_vertices(raw_vertices),
        'sprite.vertex-constants.bin':struct.pack('<712I',*(word for row in state['constants'][10:188] for word in row)),
        'sprite.constants.bin':struct.pack('<72f',*[(word>>shift&255)/255 for word in factors for shift in (16,8,0,24)])}
    # Private future-admission evidence only; no runtime consumes this yet.
    files['sprite.contract.bin'] = (struct.pack('<II',0x43533248,1)+struct.pack('<2048I',*state['setup'])+
        struct.pack('<64I',*state['setup_valid'])+program+files['sprite.vertex-constants.bin']+raw_vertices)
    out.mkdir(parents=True,exist_ok=True)
    for name, data in files.items():
        (out/name).write_bytes(data)
    report = dict(scope='private original packed-color quad preparation; no game draw accepted',
        vertex_plan=plan,pixel_definition=pixel,files={name:hashlib.sha256(data).hexdigest() for name,data in files.items()})
    (out/'sprite-shaders.json').write_text(json.dumps(report,indent=2)+'\n')
    return report['files']


if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ('xbe','snapshot','push','texture'):ap.add_argument(name,type=Path)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args()
    print(json.dumps(prepare(a.xbe,a.snapshot,a.push,a.texture,a.out),indent=2))
