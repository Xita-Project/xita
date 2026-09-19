#!/usr/bin/env python3
"""Compare the complete guarded adapter using locally owned BSP geometry.

Build --elf with test_arm_cluster_runtime.py first. Original queries, the actual
guard/admission, typed replay, publication and original allocation tail execute.
Map bytes stay in memory. Generated code and receipts belong in private output.
Inputs are synthetic portal-boundary samples, not recorded hardware queries.
Instruction counts exclude firmware copy/clear instructions, kernel latency,
cache effects and real allocator bookkeeping; they are not cycles or FPS.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import test_arm_cluster_runtime as arm
from recompiler.halo_map import HaloMap, bsp_header
from test_owned_cluster_query import Snapshot


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf', type=Path, required=True)
    p.add_argument('--maps', type=Path, nargs='+', required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--cases-per-bsp', type=int, default=24)
    p.add_argument('--profile', action='store_true', help='count ARM instructions by function')
    p.add_argument('--fp-controls', action='store_true',
                   help='repeat each input with four roundings and five FPSCR controls')
    a = p.parse_args()
    if a.cases_per_bsp < 1:
        p.error('--cases-per-bsp must be positive')
    a.out.mkdir(parents=True, exist_ok=False)
    raw_axes = (a.elf.parent / 'cluster_axes.h').read_text().split('{', 1)[1].split('}', 1)[0]
    axes = bytes(map(int, raw_axes.split(',')))
    # Larger campaign BSPs need more than the synthetic fixture's 8 MiB.
    # Put emulator control mappings above the guest arena, not inside it.
    arm.base.PT = arm.PT = 0x40000000
    arm.base.STACK = arm.STACK = 0x41000000
    arm.base.CTX = 0x42000000
    arm.base.END = arm.END = 0x43000000
    arm.base.ENV = 0x44000000
    rows = []
    receipt = {'elf_sha256': hashlib.sha256(a.elf.read_bytes()).hexdigest(),
               'complete': False, 'rows': rows, 'scope': __doc__}
    try:
        for path in a.maps:
            game = HaloMap(str(path))
            assert game.version == 5, 'requires an Xbox version-5 map'
            for bsp in game.structure_bsps():
                start, size, address = (bsp[k] for k in ('file_offset', 'size', 'load_address'))
                assert size and size <= 64 << 20 and address >= 8 << 20
                raw = game.data[start:start + size]
                assert len(raw) == size
                root = bsp_header(game, bsp)['sbsp_struct_addr']
                decoded = Snapshot(raw, address, root, axes)
                n = decoded.geometry.cluster_count
                mapped = (size + (address & 4095) + 4095) & ~4095
                arena = (8 << 20) + mapped + 4096  # unmapped sentinel page
                arm.base.SIZE = arena
                machine = arm.RuntimeMachine(a.elf, a.profile)
                u = machine.uc
                u.mem_write(machine.symbols['arm_arena_bytes'], struct.pack('<I', arena))
                page = address >> 12
                offsets = [(8 << 20) + k * 4096 for k in range(mapped // 4096)]
                u.mem_write(arm.PT + page * 4, struct.pack('<' + 'I' * len(offsets), *offsets))

                def physical(guest):
                    offset = struct.unpack('<I', u.mem_read(arm.PT + (guest >> 12) * 4, 4))[0]
                    return arm.RAM + offset + (guest & 4095)

                def write(guest, data):
                    # Used for small fixture arguments contained in one page.
                    assert len(data) <= 4096 - (guest & 4095)
                    u.mem_write(physical(guest), data)

                for case in range(a.cases_per_bsp):
                    machine.call('arm_finish')
                    u.mem_write(arm.RAM, bytes(arena))
                    machine.call('arm_prepare', (n, 1024, 4 if case % 7 == 0 else 0))
                    u.mem_write(arm.RAM + (8 << 20) + (address & 4095), raw)
                    collision = struct.unpack_from('<I', raw, root - address + 0xb4)[0]
                    for guest, value in ((0x39be58, root), (0x39be50, collision)):
                        u.mem_write(arm.RAM + (4 << 20) + guest, struct.pack('<I', value))
                    if decoded.portals:
                        portal = decoded.portals[(case // 2) % len(decoded.portals)]
                        center, cluster = list(portal.center), portal.sides[case % 2]
                        if case % 3 == 1:
                            # Move off the portal plane so small spheres also
                            # exercise one-cluster queries, not only crossings.
                            normal = decoded.planes[portal.plane].v
                            side = 2.0 if case % 2 else -2.0
                            center = [v + side * normal[k] for k, v in enumerate(center)]
                        elif case % 3 == 2:
                            center[(case // 3) % 3] += .25 if case % 2 else -.25
                    else:
                        center, cluster = [0, 0, 0], case % n
                    radius = (.125, 1, 5, 25, 1000)[case % 5]
                    sp = struct.unpack('<I', u.mem_read(machine.context + machine.layout['r'] + 16, 4))[0]
                    write(sp + 16, struct.pack('<f', radius))
                    write(sp + 68, struct.pack('<h', cluster))
                    write(sp + 80, struct.pack('<3f', *center))
                    before = bytes(u.mem_read(arm.RAM, arena))
                    context = bytes(u.mem_read(machine.context, machine.layout['size']))
                    for rounding in range(4 if a.fp_controls else 1):
                        for control in ((0, 0x10, 0x01000000, 0x02000000, 0x0300009f) if a.fp_controls else (0,)):
                            fpscr = (rounding << 22) | control
                            machine.call('arm_finish')
                            u.mem_write(arm.RAM, before); u.mem_write(machine.context, context)
                            original = machine.call('arm_original', fpscr=fpscr)
                            expected_context = bytes(u.mem_read(machine.context, machine.layout['size']))
                            expected_memory = bytes(u.mem_read(arm.RAM, arena))
                            # Original pool occupancy is the post-clamp result
                            # count. It is not the unclamped hardware census.
                            allocated = struct.unpack('<H', u.mem_read(physical(0x92030), 2))[0]
                            u.mem_write(arm.RAM, before); u.mem_write(machine.context, context)
                            u.mem_write(machine.symbols['arm_admitted'], bytes(4))
                            snapshot = machine.call('arm_snapshot', fpscr=fpscr)
                            assert bytes(u.mem_read(arm.RAM, arena)) == before
                            assert snapshot['fpscr'] == fpscr
                            allocations = bytes(u.mem_read(machine.symbols['arm_allocations'], 4))
                            candidate = machine.call('arm_candidate', fpscr=fpscr)
                            admitted = struct.unpack('<I', u.mem_read(machine.symbols['arm_admitted'], 4))[0]
                            checks = {'context': bytes(u.mem_read(machine.context, machine.layout['size'])) == expected_context,
                                      'memory': bytes(u.mem_read(arm.RAM, arena)) == expected_memory,
                                      'fpscr': candidate['fpscr'] == original['fpscr'],
                                      'admitted': admitted in (0, 1) and (n > 1 or not admitted),
                                      'no_query_allocations': bytes(u.mem_read(machine.symbols['arm_allocations'], 4)) == allocations}
                            row = {'map': path.name, 'bsp': bsp['index'], 'case': case,
                                   'bsp_sha256': hashlib.sha256(raw).hexdigest(),
                                   'map_clusters': n, 'start': cluster, 'center': center,
                                   'radius': radius, 'rounding': rounding, 'control': control,
                                   'allocated_clusters': allocated, 'admitted': admitted,
                                   'original': original, 'snapshot': snapshot, 'candidate': candidate}
                            if not all(checks.values()):
                                (a.out / 'failure.json').write_text(json.dumps(dict(row, checks=checks), indent=2))
                                raise AssertionError((path.name, bsp['index'], case, checks))
                            rows.append(row)
                    print('PASS owned-map full ARM', path.name, bsp['index'], case, 'clusters',
                          allocated, 'instructions original/typed/snapshot', original['instructions'],
                          candidate['instructions'], snapshot['instructions'], flush=True)
                machine.call('arm_finish')
        # Admission may deliberately reject cheap inputs, but a fallback-only
        # run cannot establish candidate coverage on these portal samples.
        for name in {row['map'] for row in rows}:
            assert any(row['admitted'] for row in rows if row['map'] == name), name
        receipt['complete'] = True
    finally:
        (a.out / 'result.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print('PASS', len(rows), 'complete context/arena/FPSCR comparisons; no hardware timing claim')


if __name__ == '__main__':
    main()
