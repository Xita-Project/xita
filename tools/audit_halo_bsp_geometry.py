#!/usr/bin/env python3
"""Map owned Halo CE Xbox v5 BSP resources to material vertex/index spans.

This read-only audit identifies on-disk provenance, not runtime immutability.
Output contains addresses, sizes and hashes, never vertex/texture payloads.
Use --output to retain the detailed ranges privately for draw-trace matching.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from recompiler.halo_map import HaloMap, bsp_header

# Halo 3925's stride table at 0x1E0AB4, also used by the stream binders.
STRIDES = (56, 32, 20, 8, 68, 32, 24, 36, 20, 16, 16, 8)


def audit_bsp(game, bsp):
    header = bsp_header(game, bsp)
    base, size, file_offset = (bsp[k] for k in ('load_address', 'size', 'file_offset'))

    def offset(address, length):
        if length < 0 or not base <= address <= address + length <= base + size:
            raise ValueError(f'BSP {bsp["index"]}: span {address:#x}+{length:#x} outside block')
        return file_offset + address - base

    def words(address, count):
        return struct.unpack_from('<' + str(count) + 'I', game.data, offset(address, count * 4))

    resources = {}
    for kind in ('render', 'lightmap'):
        count = header[kind + '_vertex_buffer_count']
        address = header[kind + '_vertex_buffer_address']
        for index in range(count):
            resource = address + index * 12
            if resource in resources:
                raise ValueError('Overlapping BSP resource arrays')
            common, data, lock = words(resource, 3)
            resources[resource] = dict(kind=kind, index=index, resource=resource,
                                       disk_data=data, common=common, lock=lock)

    root = header['sbsp_struct_addr']
    triangle_count, triangle_address = words(root + 0xF8, 2)
    if triangle_count:
        offset(triangle_address, triangle_count * 6)
    lightmap_count, lightmap_address = words(root + 0x104, 2)
    if lightmap_count:
        offset(lightmap_address, lightmap_count * 32)
    materials = []
    resource_uses = Counter()
    triangle_uses = bytearray(triangle_count)
    for lightmap in range(lightmap_count):
        count, address = words(lightmap_address + lightmap * 32 + 20, 2)
        if count:
            offset(address, count * 256)
        for index in range(count):
            material = address + index * 256
            first, triangles = words(material + 20, 2)
            if first + triangles > triangle_count:
                raise ValueError('Material triangle span outside BSP triangle array')
            for tri in range(first, first + triangles):
                if triangle_uses[tri]:
                    raise ValueError('Overlapping material triangle spans')
                triangle_uses[tri] = 1
            streams = []
            for kind, field in (('render', 0xB0), ('lightmap', 0xC4)):
                layout, vertices, vertex_offset, _tool_pointer, resource = words(material + field, 5)
                if resource not in resources or resources[resource]['kind'] != kind:
                    raise ValueError('Material references an unknown/wrong-kind vertex resource')
                if layout >= len(STRIDES):
                    raise ValueError(f'Unsupported vertex layout {layout}')
                # Stream binders 0x7A2F0/0x7A3D0 use the resource base directly.
                # Do not guess how to admit a cache with a nonzero offset.
                if vertex_offset:
                    raise ValueError('Nonzero material vertex offset needs a separate audit')
                stride = STRIDES[layout]
                byte_count = vertices * stride
                data = resources[resource]['disk_data']
                if byte_count:
                    offset(data, byte_count)
                resource_uses[resource] += 1
                streams.append(dict(kind=kind, layout=layout, stride=stride,
                    vertices=vertices, bytes=byte_count, resource=resource,
                    disk_data=data, physical_data=data & 0x03FFFFFF))
            if triangles:
                vals = struct.unpack_from('<' + str(triangles * 3) + 'H', game.data,
                                          offset(triangle_address + first * 6, triangles * 6))
                if max(vals) >= streams[0]['vertices']:
                    raise ValueError('BSP triangle references a missing render vertex')
                # Some materials have no lightmap stream. Do not infer one.
                if streams[1]['vertices'] and max(vals) >= streams[1]['vertices']:
                    raise ValueError('BSP triangle references a missing lightmap vertex')
            materials.append(dict(lightmap=lightmap, index=index, address=material,
                                  first_triangle=first, triangles=triangles, streams=streams))

    if any(resource_uses[r] != 1 for r in resources):
        raise ValueError('Resource/material mapping is not one-to-one; audit before using ranges')
    if not all(triangle_uses):
        raise ValueError('BSP has triangles outside the material inventory')
    streams = [s for m in materials for s in m['streams']]
    return dict(index=bsp['index'], name=bsp['name'], load_address=base, size=size,
        root=root, triangles=triangle_count, triangle_address=triangle_address,
        lightmaps=lightmap_count, material_count=len(materials),
        render_bytes=sum(s['bytes'] for s in streams if s['kind'] == 'render'),
        lightmap_bytes=sum(s['bytes'] for s in streams if s['kind'] == 'lightmap'),
        empty_streams=sum(s['bytes'] == 0 for s in streams), materials=materials)


def audit(path):
    # No header-only inflated-cache key: read the current owned file every time.
    game = HaloMap(str(path))
    if game.version != 5:
        raise ValueError('Only original Xbox cache version 5 is supported')
    bsps = [audit_bsp(game, b) for b in game.structure_bsps()]
    return dict(map=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        build=game.build, scope='On-disk provenance only; runtime writes/lifetimes remain unproved.',
        summary=dict(bsps=len(bsps), materials=sum(b['material_count'] for b in bsps),
            triangles=sum(b['triangles'] for b in bsps),
            render_bytes=sum(b['render_bytes'] for b in bsps),
            lightmap_bytes=sum(b['lightmap_bytes'] for b in bsps)), bsps=bsps)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('maps', type=Path, nargs='+')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    reports = [audit(path) for path in args.maps]
    if args.output:
        args.output.write_text(json.dumps(reports, indent=2) + '\n')
    for report in reports:
        print(json.dumps(dict(map=report['map'], **report['summary'])))


if __name__ == '__main__':
    main()
