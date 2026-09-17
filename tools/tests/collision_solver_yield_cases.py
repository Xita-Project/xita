#!/usr/bin/env python3
"""Authored synthetic packets for all fourteen solver yield callbacks.

No captured game geometry. Feed the JSON to prototype_collision_solver.py's
--cases-json option. Every case starts at the real 172CB8 caller boundary.
"""
import argparse
import copy
import json
from pathlib import Path
import struct


def f32(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def packet(name, kind, position, motion, extra=(), count=1):
    words = [[0x20000 + 4*i, f32(v)] for i, v in enumerate(position)]
    words += [[0x20010 + 4*i, f32(v)] for i, v in enumerate(motion)]
    return dict(name=name, count=count, kind=kind, budget=1, variant=12,
                fpscr=0, guest_words=words + list(extra))


def cases():
    base = []
    for name, position, motion in (
        ('after_motion', (2, 0, .5), (-1, 0, 0)),
        ('before_motion', (2, 0, .5), (1, 0, 0)),
        ('below_axis', (2, 0, -1), (-4, 0, 0)),
        ('above_axis', (2, 0, 2), (-4, 0, 0)),
        ('intersects', (2, 0, .5), (-4, 0, 0)),
    ):
        base.append(packet('capsule_' + name, 2, position, motion))
    poly = 0x14408
    base.append(packet('polygon_parallel_behind', 3, (0, 0, -1), (1, 0, 0)))
    base.append(packet('polygon_outside_moving', 3, (20, 0, .5), (1, 1, 0),
                       [[poly + 28, f32(1)]]))
    base.append(packet('polygon_outside_parallel', 3, (20, 0, .5), (0, 1, 0),
                       [[poly + 28, f32(1)]]))
    for count in (2, 3):
        words = []
        for i, axis in enumerate((2, 1, 0)[:count]):
            address = poly + 104*i
            words += [[address + 12 + 4*j, f32(int(j == axis))] for j in range(3)]
            words += [[address + 24, f32(0)], [address + 28, f32(0)],
                      [address + 32, axis]]
        base.append(packet('orthogonal_' + str(count) + '_planes', 3,
                           (1, 1, 1), (-2, -3, -4), words, count))
    ceiling = copy.deepcopy(base[-1])
    ceiling['name'] = 'orthogonal_ceiling_3_planes'
    ceiling['guest_words'] += [[0x20008, f32(-1)], [0x20018, f32(4)],
                              [poly + 20, f32(-1)]]
    base.append(ceiling)
    for name, first, second in (
        ('cross_alias', 0x20010, 0x20000),
        ('both_delta', 0x20010, 0x20010),
        ('partial_delta', 0x20014, 0x20010),
    ):
        base.append(dict(name='stationary_' + name, count=2, kind=4, budget=1,
                         variant=14, fpscr=0,
                         guest_words=[[0x9000c, first], [0x90010, second]]))
    required = [[0x85b63], [0x85b70], [0x85c63], [0x85c76],
                [0x8661d, 0x1710b6], [0x85839], [0x859a0, 0x859c2],
                [0x859b3], [0x17124d, 0x85847, 0x8660a], [0x17124d],
                [0x1713c9], [], [], []]
    for case, sites in zip(base, required):
        case['required_sites'] = sites
    result = []
    for case in base[:11]:
        for fpscr in (0, 0x400000, 0x800000, 0xc00000, 0x0300009f):
            item = copy.deepcopy(case)
            item['name'] += f'_fp{fpscr:x}'
            item['fpscr'] = fpscr
            result.append(item)
    result += base[11:]
    for index, site in ((0, 0x85b63), (1, 0x85b70), (2, 0x85c63),
                        (3, 0x85c76), (5, 0x85839), (6, 0x859a0),
                        (7, 0x859b3), (8, 0x17124d), (10, 0x1713c9)):
        item = copy.deepcopy(base[index])
        item['name'] += f'_remap_at_{site:x}'
        item['variant'] |= 5 << 4
        item['mutation_site'] = site
        result.append(item)
    for index in (2, 10):
        item = copy.deepcopy(base[index])
        item['name'] += '_top7'
        item['variant'] |= 7 << 8
        result.append(item)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--subset', choices=('full', 'fallback', 'off'), default='full')
    args = parser.parse_args()
    selected = cases()
    if args.subset != 'full':
        selected = [c for c in selected if c['fpscr'] == 0]
    if args.subset == 'off':
        selected = [c for c in selected if not c.get('mutation_site') and '_top7' not in c['name']]
    with args.out.open('x') as file:
        json.dump(selected, file, indent=2)
        file.write('\n')
