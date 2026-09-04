#!/usr/bin/env python3
"""Audit the blocker to byte-exact offline PSDEF extraction; see PSDEFS.md.

This is deliberately not an extractor. No captured bytes are copied to output,
and no map attribution or successful reproduction is inferred from similarity.
Exit status: 2 = reproduction blocked, 1 = invalid input.
"""

import argparse
from collections import Counter, defaultdict
from pathlib import Path
import struct
import sys


ARRAYS = ((0, "PSAlphaInputs"), (26, "PSAlphaOutputs"),
          (34, "PSRGBInputs"), (45, "PSRGBOutputs"))


def psdef_hash(data):
    """Match recomp/kernel/xd3d.c:psdef_hash (not FNV of all 240 bytes)."""
    if len(data) != 240:
        raise ValueError("PSDEF must contain exactly 240 bytes")
    value = 2166136261
    for offset, byte in enumerate(data):
        if 0x28 <= offset < 0x68 or 0xAC <= offset < 0xB4:
            continue
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return f"{value:08X}"


def structural_key(data):
    """Diagnostic grouping only, NOT a replacement runtime hash."""
    words = list(struct.unpack("<60I", data))
    count = words[53] & 255
    if not 1 <= count <= 8:
        raise ValueError(f"invalid combiner count: {count}")
    inactive = []
    for base, name in ARRAYS:
        for stage in range(count, 8):
            if words[base + stage]:
                inactive.append(f"{name}[{stage}]")
            words[base + stage] = 0
    for index in (*range(10, 26), 43, 44):
        words[index] = 0
    return tuple(words), inactive


def inspect_map(path):
    """Read only the cache header/index, without names or resource payloads."""
    with path.open("rb") as stream:
        size = stream.seek(0, 2)

        def read_at(offset, length):
            if offset < 0 or offset + length > size:
                raise ValueError("cache structure extends beyond file")
            stream.seek(offset)
            result = stream.read(length)
            if len(result) != length:
                raise ValueError("truncated cache structure")
            return result

        header = read_at(0, 2048)
        if header[:4] != b"daeh" or struct.unpack_from("<I", header, 4)[0] != 5:
            raise ValueError("expected decompressed Xbox version-5 cache (little-endian head)")
        index_offset = struct.unpack_from("<I", header, 0x10)[0]
        index = read_at(index_offset, 0x24)
        base, scenario, _, count = struct.unpack_from("<4I", index)
        entries = read_at(index_offset + 0x24, count * 0x20)
        classes = Counter()
        root = None
        for offset in range(0, len(entries), 0x20):
            entry = entries[offset:offset + 0x20]
            tag_class = entry[:4][::-1].decode("ascii")
            classes[tag_class] += 1
            tag_id = struct.unpack_from("<I", entry, 12)[0]
            if tag_id == scenario:
                root = tag_class
                va = struct.unpack_from("<I", entry, 20)[0]
                read_at(va - base + index_offset + 0x24, 1)
        if root != "scnr":
            raise ValueError("scenario id does not resolve to a scenario tag")
        return count, scenario, classes


def audit(directory, list_groups=False):
    groups = defaultdict(list)
    inactive_counts = Counter()
    dirty = 0
    paths = sorted(directory.glob("*.bin"))
    if not paths:
        raise ValueError("check directory contains no .bin definitions")
    for path in paths:
        data = path.read_bytes()
        if psdef_hash(data) != path.stem.upper():
            raise ValueError(f"capture filename/hash mismatch: {path.name}")
        key, inactive = structural_key(data)
        groups[key].append(path.stem.upper())
        inactive_counts.update(inactive)
        dirty += bool(inactive)
    print(f"Captures: {len(paths)}/{len(paths)} filename hashes verified")
    print(f"Nonzero inactive instructions: {dirty}/{len(paths)} definitions")
    print(f"Structures after clearing inactive instructions and constants: {len(groups)}")
    print("Nonzero inactive fields: " + ", ".join(sorted(inactive_counts)))
    if list_groups:
        for key, hashes in sorted(groups.items(), key=lambda item: item[1][0]):
            print(f"Structural group (stages={key[53] & 255}): " + " ".join(hashes))
    print("Map attribution: unknown; captures contain no map/material/pass provenance")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map", type=Path, help="decompressed Xbox cache")
    parser.add_argument("--check", type=Path, help="audit captured PSDEF directory")
    parser.add_argument("--list-groups", action="store_true", help="list every captured hash by diagnostic structure")
    args = parser.parse_args(argv)
    try:
        count, scenario, classes = inspect_map(args.map)
        print(f"{args.map.name}: {count} tags; scenario id {scenario:08X}")
        print("Shader index inventory: " + ", ".join(
            f"{name}={classes[name]}" for name in
            ("senv", "soso", "sotr", "schi", "scex", "sgla", "smet", "spla", "swat", "bitm")))
        if args.check:
            audit(args.check, args.list_groups)
        print("BLOCKED: exact definitions require runtime shader-shadow history and constants.")
        print("No definitions generated; reproduction coverage is unknown. See tools/PSDEFS.md.")
        return 2
    except (OSError, ValueError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
