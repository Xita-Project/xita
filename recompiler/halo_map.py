#!/usr/bin/env python3
"""
halo_map.py - reader for Halo: Combat Evolved (Xbox) cache files (.map).

Xbox cache layout (version 5):
  0x000  2 KB header: 'head', version, file_size, ?, tag_data_offset, tag_data_size,
         name[32] @0x20, build[32] @0x40, map_type @0x60, crc @0x64, 'foot' @0x7FC
  0x800  zlib stream -> everything else (file offsets below are in the INFLATED image)
  tag data is loaded at the fixed address TAG_BASE = 0x803A6000; every pointer inside
  the tag data is an absolute Xbox address, so  file_offset = tag_data_offset + (addr - TAG_BASE).

Tag index header (at tag_data_offset, 0x28 bytes):
  +0x00 tag array address   +0x04 scenario tag id   +0x08 checksum   +0x0C tag count
  +0x10 model part count    +0x14 model vertex data address
  +0x18 model index count   +0x1C model index data address   +0x20 'tags'
Tag entry (32 bytes): group tags[3], tag id, name address, data address, external, pad.

Usage:
    halo_map.py haloce/maps/ui.map            summary, tag groups, scenario BSPs
    halo_map.py haloce/maps/ui.map --tags     full tag list
    halo_map.py haloce/maps/ui.map --bsp      structure BSP headers + lightmap/material summary
"""

from __future__ import annotations

import argparse
import hashlib
import os
import struct
import sys
import zlib
from collections import Counter
from dataclasses import dataclass
from typing import Dict, List, Optional

TAG_BASE = 0x803A6000
HEADER_SIZE = 0x800


@dataclass
class TagEntry:
    index: int
    groups: List[str]          # primary, secondary, tertiary
    tag_id: int
    name_addr: int
    data_addr: int
    external: bool
    name: str = ""


class HaloMap:
    def __init__(self, path: str, cache_dir: Optional[str] = None):
        self.path = path
        raw = open(path, "rb").read()
        if raw[:4] != b"daeh"[::-1] and raw[:4] != b"head"[::-1] and raw[:4] != b"daeh":
            pass
        self.hdr = raw[:HEADER_SIZE]
        magic, self.version, self.file_size, _, self.tag_offset, self.tag_size = struct.unpack_from("<4sIIIII", raw, 0)
        if magic != b"daeh":
            raise ValueError(f"{path}: not a Halo cache file (magic {magic!r})")
        self.name = raw[0x20:0x40].split(b"\0")[0].decode("ascii", "replace")
        self.build = raw[0x40:0x60].split(b"\0")[0].decode("ascii", "replace")
        self.map_type, self.crc = struct.unpack_from("<II", raw, 0x60)
        # inflate (cached on disk: 30+ MB per map, 0.1 s but avoid re-doing it)
        body = None
        if cache_dir:
            os.makedirs(cache_dir, exist_ok=True)
            key = hashlib.sha1(raw[:HEADER_SIZE] + str(len(raw)).encode()).hexdigest()[:16]
            cpath = os.path.join(cache_dir, f"{os.path.basename(path)}.{key}.inflated")
            if os.path.exists(cpath):
                body = open(cpath, "rb").read()
        if body is None:
            body = raw[:HEADER_SIZE] + zlib.decompress(raw[HEADER_SIZE:])
            if cache_dir:
                open(cpath, "wb").write(body)
        self.data = body
        if len(self.data) != self.file_size:
            print(f"warning: inflated size {len(self.data)} != header file_size {self.file_size}", file=sys.stderr)
        self._parse_index()

    # --- address helpers ------------------------------------------------------------
    def off(self, addr: int) -> int:
        """Xbox address inside the tag data -> file offset in the inflated image."""
        return self.tag_offset + (addr - TAG_BASE)

    def u32(self, off: int) -> int:
        return struct.unpack_from("<I", self.data, off)[0]

    def cstr(self, addr: int, limit: int = 256) -> str:
        o = self.off(addr)
        e = self.data.find(b"\0", o, o + limit)
        return self.data[o:e if e != -1 else o + limit].decode("ascii", "replace")

    def _parse_index(self) -> None:
        o = self.tag_offset
        (self.tag_array_addr, self.scenario_id, self.checksum, self.tag_count,
         self.model_part_count, self.model_vertex_addr, self.model_index_count,
         self.model_index_addr, magic) = struct.unpack_from("<8I4s", self.data, o)
        if magic != b"sgat":
            raise ValueError(f"tag index magic {magic!r} != 'tags'")
        self.tags: List[TagEntry] = []
        self.by_id: Dict[int, TagEntry] = {}
        for i in range(self.tag_count):
            eo = self.off(self.tag_array_addr) + i * 32
            g0, g1, g2, tid, name_addr, data_addr, ext, _pad = struct.unpack_from("<4s4s4sIIIII", self.data, eo)
            groups = [g[::-1].decode("ascii", "replace").strip("\xff") for g in (g0, g1, g2)]
            t = TagEntry(i, groups, tid, name_addr, data_addr, bool(ext))
            t.name = self.cstr(name_addr) if TAG_BASE <= name_addr < TAG_BASE + self.tag_size else ""
            self.tags.append(t)
            self.by_id[tid] = t

    # --- reflexive (count, address) helper ----------------------------------------
    def reflexive(self, struct_off: int, field_off: int):
        count, addr = struct.unpack_from("<II", self.data, struct_off + field_off)
        return count, (self.off(addr) if count else 0)

    # --- scenario ---------------------------------------------------------------------
    def scenario(self) -> TagEntry:
        return self.by_id[self.scenario_id]

    def structure_bsps(self):
        """
        Scenario 'structure BSPs' block: 32-byte entries
          +0x00 bsp file offset (in the inflated image)  +0x04 bsp size
          +0x08 bsp load address                          +0x0C pad
          +0x10 tag ref {group, name addr, name len, id}
        """
        scnr = self.scenario()
        so = self.off(scnr.data_addr)
        count, bo = self.reflexive(so, 0x5A4)
        out = []
        for i in range(count):
            e = bo + i * 32
            foff, size, addr, _pad, grp, name_addr, _nlen, tid = struct.unpack_from("<IIII4sIII", self.data, e)
            out.append({"index": i, "file_offset": foff, "size": size, "load_address": addr,
                        "group": grp[::-1].decode("ascii", "replace"), "tag_id": tid,
                        "name": self.by_id[tid].name if tid in self.by_id else "?"})
        return out


def bsp_header(m: HaloMap, bsp: dict):
    """
    BSP block header (32 bytes at the BSP's file offset):
      +0x00 sbsp tag struct address   +0x04 lightmap material count?   +0x08 lightmap materials addr
      +0x0C rendered vertices?        +0x10 ...                        +0x1C 'sbsp'
    Pointers inside the block are absolute addresses relative to load_address.
    """
    o = bsp["file_offset"]
    f = struct.unpack_from("<7I4s", m.data, o)
    return {"sbsp_struct_addr": f[0], "f1": f[1], "f2": f[2], "f3": f[3], "f4": f[4], "f5": f[5], "f6": f[6],
            "magic": f[7][::-1].decode("ascii", "replace")}


def main() -> int:
    ap = argparse.ArgumentParser(prog="recompiler/halo_map.py")
    ap.add_argument("map")
    ap.add_argument("--tags", action="store_true", help="list every tag")
    ap.add_argument("--bsp", action="store_true", help="dump structure BSP headers")
    ap.add_argument("--cache", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".mapcache"),
                    help="directory for inflated map cache")
    args = ap.parse_args()

    m = HaloMap(args.map, args.cache)
    print(f"{m.path}: '{m.name}' build {m.build} type {m.map_type} version {m.version}")
    print(f"  inflated {len(m.data):,} B; tag data @ file 0x{m.tag_offset:X} ({m.tag_size:,} B) -> Xbox 0x{TAG_BASE:08X}")
    print(f"  {m.tag_count} tags; scenario id 0x{m.scenario_id:08X} = '{m.scenario().name}'")
    print(f"  model data: {m.model_part_count} parts, vertices @ 0x{m.model_vertex_addr:08X}, "
          f"{m.model_index_count} index blocks @ 0x{m.model_index_addr:08X}")
    groups = Counter(t.groups[0] for t in m.tags)
    ext = sum(1 for t in m.tags if t.external)
    print(f"  tag groups: {', '.join(f'{g}:{n}' for g, n in groups.most_common())}")
    print(f"  external (in bitmaps.map/sounds.map): {ext}")

    if args.tags:
        for t in m.tags:
            print(f"  [{t.index:4d}] {t.groups[0]} 0x{t.tag_id:08X} @0x{t.data_addr:08X}{' ext' if t.external else '    '} {t.name}")

    bsps = m.structure_bsps()
    print(f"\n  structure BSPs ({len(bsps)}):")
    for b in bsps:
        print(f"    #{b['index']} {b['name']}  file 0x{b['file_offset']:X} size {b['size']:,} B  load 0x{b['load_address']:08X}  tag 0x{b['tag_id']:08X}")
        if args.bsp:
            h = bsp_header(m, b)
            print(f"       header: sbsp struct 0x{h['sbsp_struct_addr']:08X} magic '{h['magic']}' "
                  f"f1..f6 = {h['f1']:#x} {h['f2']:#x} {h['f3']:#x} {h['f4']:#x} {h['f5']:#x} {h['f6']:#x}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
