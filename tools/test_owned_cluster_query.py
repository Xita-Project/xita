#!/usr/bin/env python3
"""Numerical query comparison on locally owned Xbox Halo CE maps.

The original reads the unmodified BSP block. The typed candidate reads compact,
independently decoded arrays. Assets and generated code stay in --out, outside
the repository. This does not establish live snapshot ownership or publication.
"""
from pathlib import Path
import argparse
import ctypes as C
import hashlib
import json
import os
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler.halo_map import HaloMap, bsp_header
from test_cluster_query import generate


class Cluster(C.Structure):
    _fields_ = [('first', C.c_uint32), ('count', C.c_uint32)]


class Portal(C.Structure):
    _fields_ = [('sides', C.c_int16 * 2), ('plane', C.c_uint32),
                ('center', C.c_float * 3), ('radius', C.c_float),
                ('first_vertex', C.c_uint32), ('vertices', C.c_uint32)]


class Point(C.Structure):
    _fields_ = [('v', C.c_float * 3)]


class Plane(C.Structure):
    _fields_ = [('v', C.c_float * 4)]


class Geometry(C.Structure):
    _fields_ = [('clusters', C.POINTER(Cluster)), ('adjacency', C.POINTER(C.c_uint16)),
                ('portals', C.POINTER(Portal)), ('vertices', C.POINTER(Point)),
                ('distance_planes', C.POINTER(Plane)), ('projection_planes', C.POINTER(Plane)),
                ('cluster_count', C.c_uint32), ('adjacency_count', C.c_uint32),
                ('portal_count', C.c_uint32), ('vertex_count', C.c_uint32),
                ('distance_plane_count', C.c_uint32), ('projection_plane_count', C.c_uint32),
                ('axes', (C.c_int16 * 2) * 6)]


class Input(C.Structure):
    _fields_ = [('center', C.c_float * 3), ('radius', C.c_float),
                ('start', C.c_int16), ('epoch', C.c_uint32),
                ('budget', C.c_uint32), ('visited', C.POINTER(C.c_uint32))]


class Result(C.Structure):
    _fields_ = [('clusters', C.c_uint16 * 64), ('count', C.c_uint32),
                ('epoch', C.c_uint32), ('changed', C.c_uint32 * 8),
                ('backedges', C.c_uint32), ('portal_tests', C.c_uint32),
                ('maximum_depth', C.c_uint32)]


class Snapshot:
    """Own every array; never retain a pointer into map bytes or a live guest."""
    def __init__(self, raw, base, root, axes):
        def read(address, size):
            offset = address - base
            assert 0 <= offset <= len(raw) and size <= len(raw) - offset, 'BSP pointer bounds'
            return raw[offset:offset + size]

        def unpack(fmt, address):
            return struct.unpack(fmt, read(address, struct.calcsize(fmt)))

        def block(address, stride, maximum):
            count, pointer = unpack('<II', address)
            assert count <= maximum, ('block count', count, maximum)
            read(pointer, count * stride) if count else None
            return count, pointer

        count, cp = block(root + 0x134, 104, 256)
        pc, pp = block(root + 0x154, 64, 32768)
        collision_count, collision = block(root + 0xb0, 96, 1)
        assert collision_count == 1 and count, 'unsupported collision/cluster layout'
        planes_count, planes = block(collision + 0xc, 16, 1 << 20)
        self.clusters = (Cluster * count)()
        self.portals = (Portal * pc)()
        adjacency, vertices, used_planes = [], [], {}
        for i in range(count):
            n, ptr = block(cp + i * 104 + 0x5c, 2, 32767)
            self.clusters[i] = Cluster(len(adjacency), n)
            if n:
                adjacency.extend(unpack('<' + 'H' * n, ptr))
        for i in range(pc):
            ptr = pp + i * 64
            a, b, plane = unpack('<hhI', ptr)
            assert plane < planes_count, 'portal plane index'
            compact_plane = used_planes.setdefault(plane, len(used_planes))
            center_radius = unpack('<4f', ptr + 8)
            n, vp = block(ptr + 0x34, 12, 128)
            self.portals[i] = Portal((C.c_int16 * 2)(a, b), compact_plane,
                (C.c_float * 3)(*center_radius[:3]), center_radius[3], len(vertices), n)
            vertices.extend(Point((C.c_float * 3)(*unpack('<3f', vp + k * 12))) for k in range(n))
        self.adjacency = (C.c_uint16 * len(adjacency))(*adjacency)
        self.vertices = (Point * len(vertices))(*vertices)
        # The raw map has one collision BSP. Both original plane roots point to
        # it in this fixture; synthetic tests separately cover distinct roots.
        self.planes = (Plane * max(1, len(used_planes)))()
        for old, new in used_planes.items():
            self.planes[new] = Plane((C.c_float * 4)(*unpack('<4f', planes + old * 16)))
        self.geometry = Geometry(self.clusters, self.adjacency, self.portals, self.vertices,
            self.planes, self.planes, count, len(adjacency), pc, len(vertices),
            len(used_planes), len(used_planes))
        C.memmove(self.geometry.axes, axes, 24)
        self.array_bytes = sum(C.sizeof(a) for a in
            (self.clusters, self.adjacency, self.portals, self.vertices, self.planes))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'out'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--maps', type=Path, nargs='+', required=True)
    parser.add_argument('--cases-per-bsp', type=int, default=512)
    parser.add_argument('--native-snapshot', action='store_true',
                        help='construct candidate arrays with the runtime C builder')
    a = parser.parse_args()
    assert a.cases_per_bsp > 0
    a.out.mkdir(parents=True, exist_ok=True)
    generate(a.xbe, a.manifest, a.out)
    # Lookup bytes are supplied by the hash-checked original image generator.
    lookup = (a.out / 'cluster_axes.h').read_text().split('{', 1)[1].split('}', 1)[0]
    axes = bytes(map(int, lookup.split(',')))
    library = a.out / 'owned-map-oracle.so'
    exports = a.out / 'exports.map'
    exports.write_text('{ global: xv_test_map_*; xv_cluster_geometry_valid; local: *; };\n')
    command = [os.environ.get('CC', 'cc'), '-O2', '-g', '-std=gnu11', '-shared', '-fPIC',
        '-fno-strict-aliasing', '-ffp-contract=off', '-frounding-math',
        '-ffunction-sections', '-fdata-sections',
        '-fsanitize=undefined', '-fno-sanitize-recover=all',
        '-I' + str(ROOT / 'recomp'), str(a.out / 'reference.c'),
        str(ROOT / 'tools/tests/cluster_query_map.c'),
        str(ROOT / 'recomp/kernel/xk_cluster_snapshot.c'),
        str(ROOT / 'recomp/kernel/xk_cluster_query.c'), str(ROOT / 'recomp/xv_x86rt.c'),
        '-Wl,--wrap=xv_preempt,--gc-sections,-z,defs,--version-script=' + str(exports),
        '-lm', '-o', str(library)]
    subprocess.run(command, check=True)
    (a.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    lib = C.CDLL(str(library))
    layout = (C.c_size_t * 7).in_dll(lib, 'xv_test_map_layout')
    assert list(layout) == [C.sizeof(t) for t in (Cluster, Portal, Point, Plane, Geometry, Input, Result)]
    lib.xv_test_map_load.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_void_p]
    lib.xv_test_map_load.restype = C.c_int
    lib.xv_cluster_geometry_valid.argtypes = [C.POINTER(Geometry)]
    lib.xv_cluster_geometry_valid.restype = C.c_int
    lib.xv_test_map_query.argtypes = [C.POINTER(Geometry), C.POINTER(Input), C.c_uint,
                                    C.POINTER(Result), C.POINTER(C.c_uint32)]
    lib.xv_test_map_query.restype = C.c_int
    lib.xv_test_map_free.restype = None
    lib.xv_test_map_snapshot.restype = C.POINTER(Geometry)
    lib.xv_test_map_snapshot_bytes.restype = C.c_size_t
    rows = []
    try:
        for path in a.maps:
            m = HaloMap(str(path))
            assert m.version == 5, 'requires an Xbox version-5 map'
            for bsp in m.structure_bsps():
                start, size, base = (bsp[k] for k in ('file_offset', 'size', 'load_address'))
                raw = m.data[start:start + size]
                assert len(raw) == size
                root = bsp_header(m, bsp)['sbsp_struct_addr']
                snapshot = Snapshot(raw, base, root, axes)
                assert lib.xv_cluster_geometry_valid(C.byref(snapshot.geometry)), (path, bsp['index'])
                assert lib.xv_test_map_load(raw, size, base, root, axes)
                candidate = C.pointer(snapshot.geometry)
                if a.native_snapshot:
                    candidate = lib.xv_test_map_snapshot()
                    assert candidate, (path.name, bsp['index'], 'snapshot creation')
                    native = candidate.contents
                    assert native.cluster_count == snapshot.geometry.cluster_count
                    assert native.portal_count == snapshot.geometry.portal_count
                    for i, portal in enumerate(snapshot.portals):
                        other = native.portals[i]
                        assert list(other.sides) == list(portal.sides)
                        assert list(other.center) == list(portal.center) and other.radius == portal.radius
                        assert list(native.distance_planes[other.plane].v) == list(snapshot.planes[portal.plane].v)
                        assert list(native.projection_planes[other.plane].v) == list(snapshot.planes[portal.plane].v)
                        assert other.vertices == portal.vertices
                        for k in range(portal.vertices):
                            assert list(native.vertices[other.first_vertex+k].v) == list(snapshot.vertices[portal.first_vertex+k].v)
                    for i, cluster in enumerate(snapshot.clusters):
                        other = native.clusters[i]
                        assert other.count == cluster.count
                        assert list(native.adjacency[other.first:other.first+other.count]) == list(
                            snapshot.adjacency[cluster.first:cluster.first+cluster.count])
                stats, result = (C.c_uint32 * 3)(), Result()
                visited = (C.c_uint32 * 256)()
                rng = random.Random(0x56670 + bsp['index'])
                row = dict(map=path.name, bsp=bsp['index'], bsp_sha256=hashlib.sha256(raw).hexdigest(),
                    clusters=snapshot.geometry.cluster_count, portals=snapshot.geometry.portal_count,
                    snapshot_array_bytes=snapshot.array_bytes, cases=0, declines=0,
                    native_snapshot_bytes=lib.xv_test_map_snapshot_bytes() if a.native_snapshot else None,
                    maximum_result=0, maximum_portal_tests=0)
                for case in range(a.cases_per_bsp):
                    # Exercise real portal boundaries from each side, nearby
                    # points, and radii spanning single-cluster to whole-map work.
                    if len(snapshot.portals):
                        portal = snapshot.portals[(case // 2) % len(snapshot.portals)]
                        center = list(portal.center)
                        cluster = portal.sides[case % 2]
                        if case % 3:
                            center = [v + rng.uniform(-2, 2) for v in center]
                    else:
                        center, cluster = [0, 0, 0], case % snapshot.geometry.cluster_count
                    radius = (0, .125, 1, 5, 25, 1000)[
                        (case // 2 + case // max(1, 2*len(snapshot.portals))) % 6]
                    epoch = 0xffffffff if case % 11 == 0 else 100
                    for i in range(256):
                        visited[i] = ((epoch + 1) & 0xffffffff) if case % 17 == 0 and i % 7 == 0 else 0xabcd0000 + i
                    data = Input((C.c_float * 3)(*center), radius, cluster, epoch, 1000000, visited)
                    for rounding in range(4):
                        code = lib.xv_test_map_query(candidate, C.byref(data), rounding,
                                                     C.byref(result), stats)
                        assert code >= 0, (path.name, bsp['index'], case, rounding, code,
                                           list(stats), result.count, list(center), radius)
                        row['cases'] += 1
                        row['declines'] += code
                        if not code:
                            row['maximum_result'] = max(row['maximum_result'], result.count)
                            row['maximum_portal_tests'] = max(row['maximum_portal_tests'], result.portal_tests)
                assert row['declines'] == 0, row
                rows.append(row)
                (a.out / 'result.json').write_text(json.dumps(rows, indent=2) + '\n')
                print('PASS owned map', path.name, 'BSP', bsp['index'], row['cases'], 'cases,',
                      row['snapshot_array_bytes'], 'snapshot array bytes', flush=True)
    finally:
        lib.xv_test_map_free()
    print('PASS', sum(r['cases'] for r in rows), 'owned-map numerical comparisons;',
          'no live ownership/publication proof')


if __name__ == '__main__':
    main()
