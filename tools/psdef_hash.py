#!/usr/bin/env python3
"""Canonical X_D3DPIXELSHADERDEF identity (stdlib only).

Run with no arguments to validate legacy capture filenames and list groups.
See PSDEFS.md for the byte layout and source-equivalence proof.
"""
import argparse
from collections import defaultdict
from pathlib import Path
import struct


def original_hash(data):
    if len(data) != 0xF0:
        raise ValueError('PSDEF must contain exactly 240 bytes')
    h = 2166136261
    for i, byte in enumerate(data):
        if 0x28 <= i < 0x68 or 0xAC <= i < 0xB4:
            continue
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def canonicalize(data):
    if len(data) != 0xF0:
        raise ValueError('PSDEF must contain exactly 240 bytes')
    d = bytearray(data)
    n = min(d[0xD4], 8)
    for base in (0x00, 0x68, 0x88, 0xB4):
        d[base + 4*n:base + 32] = bytes(4 * (8-n))
    for off in (0xE4, 0xE8):
        word, = struct.unpack_from('<I', d, off)
        struct.pack_into('<I', d, off, word & ((1 << (4*n)) - 1))
    d[0x28:0x68] = bytes(0x40)
    d[0xAC:0xB4] = bytes(8)
    return bytes(d)


def canonical_hash(data):
    return original_hash(canonicalize(data))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('directory', nargs='?', type=Path,
                    default=Path(__file__).resolve().parents[1] / 'shaders/psdefs')
    args = ap.parse_args()
    groups = defaultdict(list)
    structures = {}
    paths = sorted(args.directory.glob('*.bin'))
    if not paths:
        raise ValueError('no captures found')
    for path in paths:
        data = path.read_bytes()
        if original_hash(data) != int(path.stem, 16):
            raise ValueError(f'{path}: original hash does not match filename')
        canonical = canonicalize(data)
        h = canonical_hash(data)
        if h in structures and structures[h] != canonical:
            raise ValueError(f'canonical FNV collision: {h:08X}')
        structures[h] = canonical
        assert canonicalize(canonical) == canonical
        groups[h].append(path.stem)
    print(f'Original hashes: {len(paths)}/{len(paths)} match filenames')
    for h, members in sorted(groups.items()):
        print(f'{h:08X} {len(members):3d} {" ".join(members)}')
    print(f'{len(paths)} definitions -> {len(groups)} canonical programs')


if __name__ == '__main__':
    main()
