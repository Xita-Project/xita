#!/usr/bin/env python3
"""Inventory owned Xbox Halo CE model vertex resources; no immutability claim.

Validates resource/part identity and vertex payload bounds. This does not audit
model indices, transforms, runtime writers or GPU lifetime. Keep output private.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from recompiler.halo_map import HaloMap, TAG_BASE


def audit_models(game):
    if game.version != 5:
        raise ValueError('Only original Xbox cache version 5 is supported')

    def offset(address, size):
        if size < 0 or not TAG_BASE <= address <= address + size <= TAG_BASE + game.tag_size:
            raise ValueError('Model span outside loaded tag data')
        result = game.off(address)
        if result < 0 or result + size > len(game.data):
            raise ValueError('Model span outside inflated cache')
        return result

    def words(address, count):
        return struct.unpack_from('<' + str(count) + 'I', game.data, offset(address, count * 4))

    resources = {}
    if game.model_part_count:
        offset(game.model_vertex_addr, game.model_part_count * 12)
    for index in range(game.model_part_count):
        address = game.model_vertex_addr + index * 12
        common, data, lock = words(address, 3)
        resources[address] = dict(resource=address, common=common, disk_data=data, lock=lock)
    uses = Counter()
    streams = []
    for tag in game.tags:
        if tag.groups[0] != 'mode':
            continue
        count, address = words(tag.data_addr + 0xD0, 2)
        if count:
            offset(address, count * 48)
        for geometry in range(count):
            part_count, part_address = words(address + geometry * 48 + 0x24, 2)
            if part_count:
                offset(part_address, part_count * 104)
            for part in range(part_count):
                # Same Xbox model-part descriptors used by halo_scene_export.
                descriptor = part_address + part * 104 + 0x54
                layout, vertices, vertex_offset, _tool_pointer, resource = words(descriptor, 5)
                if layout != 5 or vertex_offset:
                    raise ValueError('Model layout/offset requires a separate audit')
                if resource not in resources:
                    raise ValueError('Model part references an unknown vertex resource')
                data = resources[resource]['disk_data']
                size = vertices * 32
                if size:
                    offset(data, size)
                uses[resource] += 1
                streams.append(dict(kind='model', resource=resource, disk_data=data,
                    physical_data=data & 0x03FFFFFF, layout=layout, stride=32,
                    vertices=vertices, bytes=size, tag_id=tag.tag_id, tag_name=tag.name,
                    geometry=geometry, part=part))
    if any(uses[address] != 1 for address in resources):
        raise ValueError('Model part/resource relationship is not one-to-one')
    return dict(resources=len(resources), vertex_bytes=sum(s['bytes'] for s in streams),
                streams=streams)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('maps', type=Path, nargs='+')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    results = []
    for path in args.maps:
        result = dict(map=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                      **audit_models(HaloMap(str(path))))
        results.append(result)
        print(json.dumps({k: v for k, v in result.items() if k != 'streams'}), flush=True)
    if args.output:
        args.output.write_text(json.dumps(results, indent=2) + '\n')


if __name__ == '__main__':
    main()
