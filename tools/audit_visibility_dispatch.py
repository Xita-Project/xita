#!/usr/bin/env python3
"""Identify the visibility path selected by owned Halo 3925 BSPs.

The engine selects a subcluster or per-triangle path using the first cluster.
Report structural counts, not runtime hotness or immutable-buffer guarantees.
No game payloads are emitted. Requires iced-x86 for independent XBE checks.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler.halo_map import HaloMap, bsp_header


def verify_image(path):
    from recompiler.xita_recomp import Image
    from iced_x86 import Decoder, Mnemonic, OpKind
    img = Image(str(path))
    digest = hashlib.sha256(img.data).hexdigest()
    if digest != '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae':
        raise ValueError('unsupported executable revision')
    signatures = [
        (0x539c0, 0x53b13, '0af45479326df633b82de3154bc3f975a487c7ccca879cadf59e708778e7fafe'),
        (0x12420, 0x1250d, 'c84badcd6c908457f67af35db47261de9fc717f8eac42e160dd515ecc0ceb492'),
    ]
    for lo, hi, expected in signatures:
        raw = img.bytes_at(lo, hi-lo)
        if hashlib.sha256(raw).hexdigest() != expected:
            raise ValueError('visibility code signature mismatch')
        ins = list(Decoder(32, raw, ip=lo))
        if any(i.is_invalid for i in ins) or ins[-1].next_ip != hi:
            raise ValueError('visibility instruction boundary mismatch')
    sites = {0x53af2: 0x52e10, 0x53b0b: 0x537e0,
             0x52ec1: 0x5c300, 0x53905: 0x12420,
             0x53923: 0x12420, 0x53944: 0x12420}
    for pc, target in sites.items():
        ins = Decoder(32, img.bytes_at(pc, 15), ip=pc).decode()
        if (ins.mnemonic != Mnemonic.CALL or ins.op0_kind != OpKind.NEAR_BRANCH32
                or ins.near_branch_target != target):
            raise ValueError('visibility child call mismatch at ' + hex(pc))
    if img.bytes_at(0x1f0a68, 4) != bytes(4):
        raise ValueError('outcode comparison constant changed')
    return dict(xbe_sha256=digest, verified_call_sites={hex(k): hex(v) for k,v in sites.items()},
                triangle_outcode_threshold=0)


def audit_bsp(game, bsp):
    header = bsp_header(game, bsp)
    base, size, start = (bsp[k] for k in ('load_address', 'size', 'file_offset'))
    def offset(address, length):
        if not 0 <= length or not base <= address <= address+length <= base+size:
            raise ValueError('visibility span outside BSP')
        position = start + address - base
        if position + length > len(game.data):
            raise ValueError('visibility span outside inflated map')
        return position
    def words(address, count):
        return struct.unpack_from('<' + str(count) + 'I', game.data, offset(address, count*4))
    root = header['sbsp_struct_addr']
    triangles = words(root + 0xf8, 1)[0]
    count, clusters = words(root + 0x134, 2)
    if not 0 < count <= 512:
        raise ValueError('unexpected cluster count')
    offset(clusters, count * 0x68)
    rows = []
    for i in range(count):
        n, subclusters = words(clusters + i*0x68 + 0x34, 2)
        if n: offset(subclusters, n*36)
        refs = 0
        for j in range(n):
            surfaces, indices = words(subclusters + j*36 + 0x18, 2)
            if surfaces:
                values = words(indices, surfaces)
                if max(values) >= triangles:
                    raise ValueError('subcluster surface outside triangle array')
            refs += surfaces
        rows.append(dict(cluster=i, subclusters=n, surface_references=refs))
    return dict(index=bsp['index'], clusters=count, triangles=triangles,
                path='subcluster_bounds_52E10' if rows[0]['subclusters'] else 'triangle_outcodes_537E0',
                first_cluster_subclusters=rows[0]['subclusters'],
                subclusters=sum(r['subclusters'] for r in rows),
                surface_references=sum(r['surface_references'] for r in rows),
                cluster_rows=rows)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe', required=True, type=Path)
    p.add_argument('--maps', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    proof = verify_image(a.xbe)
    records = []
    for path in sorted(a.maps.glob('*.map')):
        game = HaloMap(str(path))
        if game.version != 5:
            raise ValueError('only Xbox version 5 maps are supported')
        records.append(dict(map=game.name, file_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                            bsps=[audit_bsp(game, b) for b in game.structure_bsps()]))
    if not records: raise ValueError('no maps found')
    result = dict(result='PASS', proof=proof, maps=records,
                  scope='On-disk selection before runtime mutation; counts are not measured per-frame calls')
    with a.output.open('x') as f: json.dump(result, f, indent=2); f.write('\n')
    print(json.dumps(dict(result='PASS', maps=len(records),
        bsps=sum(len(r['bsps']) for r in records),
        triangle_paths=[dict(map=r['map'], bsp=b['index']) for r in records for b in r['bsps']
                        if b['path']=='triangle_outcodes_537E0'])))


if __name__ == '__main__': main()
