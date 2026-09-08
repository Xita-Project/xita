#!/usr/bin/env python3
"""
halo_scene_export.py - export the models of a Halo (Xbox) map as an Xita
"scene pack" the runtime can replay through the D3D HLE without any game code.

The pack reproduces what the real game would have in guest memory:
  * the map's tag data, loaded at its fixed address 0x803A6000, so every pre-baked
    D3DVertexBuffer / D3DIndexBuffer struct inside it is valid as-is;
  * each referenced bitmap's base level, converted to the SGX layout (DXT blocks
    twiddled, uncompressed pixels as stored) and placed in guest RAM, with an Xbox
    D3DTexture header (X_D3DPixelContainer) the HLE can SetTexture() directly;
  * a draw list (one record per model part) and a camera.

Pack format (little endian), consumed by xv_scene.c:
  'XVSC' u32 version=3
  u32 nblobs   { u32 guest_addr, u32 size, u32 file_offset }[nblobs]
  u32 ndraws   { u32 vb_struct, u32 ib_raw, u32 index_count, u32 prim,
                 u32 tex_struct, u32 flags(bit0 alpha blend, bit1 no depth write),
                 f32 world[12] (3x4 row-major: rotation rows + translation),
                 f32 uv_scale[2] (model base map u/v scale) }[ndraws]
  f32 eye[3], target[3], fov_deg, znear, zfar
  blob bytes...

Usage: halo_scene_export.py haloce/maps/ui.map -o assets/ui_scene.bin [--models NAME ...]
"""

from __future__ import annotations

# Support running this script directly from any working directory.
if __package__ in (None, ""):
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recompiler.halo_map import HaloMap, TAG_BASE  # noqa: E402

GUEST_MASK = 0x03FFFFFF
TEX_STRUCT_BASE = 0x00F00000          # X_D3DPixelContainer table
TEX_PIXEL_BASE = 0x01000000           # pixel blobs, 4 KB aligned

# Halo bitmap format -> (Xbox D3DFMT, bits per pixel, is_dxt, block bytes)
BITMAP_FORMATS = {
    10: (0x07, 32, False, 0),          # x8r8g8b8
    11: (0x06, 32, False, 0),          # a8r8g8b8
    14: (0x0C, 4, True, 8),            # dxt1
    15: (0x0E, 8, True, 16),           # dxt3
    16: (0x0F, 8, True, 16),           # dxt5
    6:  (0x05, 16, False, 0),          # r5g6b5
    8:  (0x02, 16, False, 0),          # a1r5g5b5
    9:  (0x04, 16, False, 0),          # a4r4g4b4
    1:  (0x00, 8, False, 0),           # y8  -> L8
    3:  (0x1A, 16, False, 0),          # a8y8 -> A8L8
}
BITMAP_FLAG_SWIZZLED = 0x08


def log2(n: int) -> int:
    return n.bit_length() - 1


def twiddle(x: int, y: int, w: int, h: int) -> int:
    """PowerVR twiddled (Morton) index for a w x h grid; non-square: square blocks
    of the smaller dimension laid along the larger axis (== NV2A swizzle rule)."""
    mn = min(w, h)
    bits = log2(mn)
    idx = 0
    for b in range(bits):
        idx |= ((x >> b) & 1) << (2 * b)
        idx |= ((y >> b) & 1) << (2 * b + 1)
    # remaining high bits of the larger dimension
    if w > h:
        idx |= (x >> bits) << (2 * bits)
    elif h > w:
        idx |= (y >> bits) << (2 * bits)
    return idx


def reorder_dxt(src: bytes, w: int, h: int, block_bytes: int) -> bytes:
    bw, bh = max(w // 4, 1), max(h // 4, 1)
    out = bytearray(len(src[:bw * bh * block_bytes]))
    for by in range(bh):
        for bx in range(bw):
            s = (by * bw + bx) * block_bytes
            d = twiddle(bx, by, bw, bh) * block_bytes
            out[d:d + block_bytes] = src[s:s + block_bytes]
    return bytes(out)


class SceneExporter:
    def __init__(self, m: HaloMap):
        self.m = m
        self.blobs = []              # (guest_addr, bytes)
        self.draws = []
        self.textures = {}           # bitmap tag id -> tex struct guest addr
        self.next_pixel = TEX_PIXEL_BASE
        self.tex_structs = bytearray()
        self.bounds = [float("inf")] * 3 + [float("-inf")] * 3
        self.model_centres = {}
        self.model_frames = {}

    # ---- tag helpers -------------------------------------------------------------
    def tag_by_name(self, group: str, name: str):
        for t in self.m.tags:
            if t.groups[0] == group and t.name == name:
                return t
        return None

    def bitmap_dims(self, tag):
        bo = self.m.off(tag.data_addr)
        count, addr = struct.unpack_from("<II", self.m.data, bo + 0x60)
        if not count:
            return 0
        _sig, w, h = struct.unpack_from("<4sHH", self.m.data, self.m.off(addr))
        return w * h

    def first_bitmap_ref(self, struct_off: int, span: int = 0x200):
        """Best base-map candidate inside a shader struct: the largest referenced bitmap
        that is not a mask (chicago shaders list e.g. 'star mask' before 'space')."""
        d = self.m.data
        best, best_px = None, -1
        for fo in range(0, span, 4):
            if d[struct_off + fo:struct_off + fo + 4][::-1] == b"bitm":
                tid = struct.unpack_from("<I", d, struct_off + fo + 12)[0]
                t = self.m.by_id.get(tid)
                if not t:
                    continue
                px = self.bitmap_dims(t)
                if "mask" in t.name.lower():
                    px //= 64
                if px > best_px:
                    best, best_px = t, px
        return best

    # ---- textures ------------------------------------------------------------------
    def texture_for_bitmap(self, tag) -> int:
        if tag.tag_id in self.textures:
            return self.textures[tag.tag_id]
        m = self.m
        bo = m.off(tag.data_addr)
        count, addr = struct.unpack_from("<II", m.data, bo + 0x60)
        if not count:
            return 0
        e = m.off(addr)                          # bitmap data entry 0
        _sig, w, h, depth, btype, fmt, flags = struct.unpack_from("<4sHHHHHH", m.data, e)
        mips, _pad, poff, psize = struct.unpack_from("<HHII", m.data, e + 0x14)
        if fmt not in BITMAP_FORMATS or btype != 0:
            print(f"  skip bitmap {tag.name}: format {fmt} type {btype} unsupported", file=sys.stderr)
            return 0
        xfmt, bpp, is_dxt, block = BITMAP_FORMATS[fmt]
        base_size = (w * h * bpp) // 8
        pixels = m.data[poff:poff + base_size]
        if is_dxt:
            pixels = reorder_dxt(pixels, w, h, block)
            layout = "DXT blocks twiddled"
        elif flags & BITMAP_FLAG_SWIZZLED:
            layout = "swizzled (as stored)"
        else:
            layout = "LINEAR (as stored)"
            xfmt = {0x06: 0x12, 0x07: 0x1E, 0x05: 0x11}.get(xfmt, xfmt)
        # place pixels
        addr_px = self.next_pixel
        self.next_pixel = (addr_px + len(pixels) + 0xFFF) & ~0xFFF
        self.blobs.append((addr_px, pixels))
        # X_D3DPixelContainer {Common, Data, Lock, Format, Size}
        if xfmt in (0x12, 0x1E, 0x11):
            fmt_word = xfmt << 8
            size_word = ((w - 1) & 0xFFF) | (((h - 1) & 0xFFF) << 12) | ((((w * bpp // 8) // 64) - 1) << 24)
        else:
            fmt_word = (xfmt << 8) | (1 << 16) | (log2(w) << 20) | (log2(h) << 24)
            size_word = 0
        ts = TEX_STRUCT_BASE + len(self.tex_structs)
        self.tex_structs += struct.pack("<5I", 0, addr_px, 0, fmt_word, size_word)
        self.tex_structs += b"\0" * 12                       # pad to 32 B per entry
        self.textures[tag.tag_id] = ts
        print(f"  texture {tag.name}: {w}x{h} halo fmt {fmt} -> xbox 0x{xfmt:02X}, {len(pixels)} B, {layout} @ 0x{addr_px:08X}")
        return ts

    # ---- models ----------------------------------------------------------------------
    def scenery_transform(self, model_name: str):
        """World transform of the first scenery placement whose palette entry's model
        is `model_name`: rows of R = Rz(yaw) * Ry(pitch) * Rx(roll), plus translation."""
        import math
        m = self.m
        scnr = m.scenario(); so = m.off(scnr.data_addr)
        pc, pa = struct.unpack_from("<II", m.data, so + 0x21C)
        sc, sa = struct.unpack_from("<II", m.data, so + 0x210)
        palette = []
        for i in range(pc):
            _g, _na, _nl, tid = struct.unpack_from("<4sIII", m.data, m.off(pa) + i * 48)
            t = m.by_id.get(tid)
            mdl = None
            if t:                                   # scenery tag -> model reference @ +0x28
                sco = m.off(t.data_addr)
                _g2, _na2, _nl2, mid = struct.unpack_from("<4sIII", m.data, sco + 0x28)
                mdl = m.by_id.get(mid)
            palette.append(mdl.name if mdl else (t.name if t else None))
        for i in range(sc):
            e = m.off(sa) + i * 72
            ti = struct.unpack_from("<h", m.data, e)[0]
            if 0 <= ti < len(palette) and palette[ti] == model_name:
                pos = struct.unpack_from("<3f", m.data, e + 8)
                yaw, pitch, roll = struct.unpack_from("<3f", m.data, e + 20)
                cy, sy, cp, sp, cr, sr = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch), math.cos(roll), math.sin(roll)
                rz = [[cy, -sy, 0], [sy, cy, 0], [0, 0, 1]]
                ry = [[cp, 0, sp], [0, 1, 0], [-sp, 0, cp]]
                rx = [[1, 0, 0], [0, cr, -sr], [0, sr, cr]]
                def mul(a, b): return [[sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)] for r in range(3)]
                R = mul(mul(rz, ry), rx)
                print(f"  placement for {model_name}: pos {pos}, yaw/pitch/roll {math.degrees(yaw):.1f}/{math.degrees(pitch):.1f}/{math.degrees(roll):.1f} deg")
                return [R[0][0], R[0][1], R[0][2], R[1][0], R[1][1], R[1][2], R[2][0], R[2][1], R[2][2], pos[0], pos[1], pos[2]]
        return [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0]

    def export_model(self, name: str, flags_override=None):
        m = self.m
        world = self.scenery_transform(name)
        tag = self.tag_by_name("mode", name)
        if not tag:
            print(f"  model {name}: not found", file=sys.stderr)
            return
        mo = m.off(tag.data_addr)
        uscale, vscale = struct.unpack_from("<2f", m.data, mo + 0x30)
        if uscale == 0.0: uscale = 1.0
        if vscale == 0.0: vscale = 1.0
        mb = [float("inf")] * 3 + [float("-inf")] * 3
        gcount, gaddr = struct.unpack_from("<II", m.data, mo + 0xD0)
        scount, saddr = struct.unpack_from("<II", m.data, mo + 0xDC)
        shaders = []
        for si in range(scount):
            so = m.off(saddr) + si * 32
            grp, _na, _nl, tid = struct.unpack_from("<4sIII", m.data, so)
            shaders.append((grp[::-1].decode("ascii", "replace"), m.by_id.get(tid)))
        print(f"model {name}: {gcount} geometries, shaders: {[(g, t.name if t else None) for g, t in shaders]}")
        for gi in range(gcount):
            go = m.off(gaddr) + gi * 48
            pcount, paddr = struct.unpack_from("<II", m.data, go + 0x24)
            for pi in range(pcount):
                po = m.off(paddr) + pi * 104
                shader_idx = struct.unpack_from("<H", m.data, po + 4)[0]
                itype, icount, iraw, _ibstruct, vtype, vcount, _z, _vbword, vbstruct = struct.unpack_from("<9I", m.data, po + 0x44)
                if itype != 1 or vtype != 5 or not (TAG_BASE <= vbstruct < TAG_BASE + m.tag_size):
                    print(f"  geom {gi} part {pi}: unexpected part encoding (itype {itype}, vtype {vtype}); skipped")
                    continue
                # vertex bounds (position = first 12 bytes of each 32-byte vertex)
                vdata = struct.unpack_from("<I", m.data, m.off(vbstruct) + 4)[0]
                vo = m.off(vdata)
                for v in range(vcount):
                    x, y, z = struct.unpack_from("<3f", m.data, vo + v * 32)
                    for k, c in enumerate((x, y, z)):
                        mb[k] = min(mb[k], c); mb[3 + k] = max(mb[3 + k], c)
                        self.bounds[k] = min(self.bounds[k], c)
                        self.bounds[3 + k] = max(self.bounds[3 + k], c)
                tex = 0
                flags = 0
                if shader_idx < len(shaders) and shaders[shader_idx][1]:
                    sgrp, stag = shaders[shader_idx]
                    bm = self.first_bitmap_ref(m.off(stag.data_addr))
                    if bm:
                        tex = self.texture_for_bitmap(bm)
                    if sgrp == "sotr":
                        flags |= 1 | 2                      # transparent generic (planets): blend, no depth write
                    elif sgrp == "schi" and not name.startswith("sky"):
                        flags |= 1 | 2                      # chicago on models: blend
                if name.startswith("sky"):
                    flags |= 4 | 2                              # sky: unlit backdrop, no depth write
                if flags_override is not None:
                    flags = flags_override
                self.draws.append((vbstruct, iraw, icount, 1, tex, flags, world, (uscale, vscale)))
                print(f"  geom {gi} part {pi}: shader {shader_idx} {shaders[shader_idx][0] if shader_idx < len(shaders) else '?'}, "
                      f"{vcount} verts @0x{vdata:08X}, {icount} strip indices @0x{iraw:08X}, tex 0x{tex:08X}, flags {flags}")

        if mb[0] != float("inf"):
            cx, cy, cz = [(mb[k] + mb[3 + k]) / 2 for k in range(3)]
            R, T = world[:9], world[9:]
            wc = [R[0] * cx + R[1] * cy + R[2] * cz + T[0], R[3] * cx + R[4] * cy + R[5] * cz + T[1],
                  R[6] * cx + R[7] * cy + R[8] * cz + T[2]]
            self.model_centres[name] = wc
            self.model_frames[name] = (world, mb)
            print(f"  {name}: uv scale {uscale:.3f}x{vscale:.3f}, model bounds x[{mb[0]:.1f},{mb[3]:.1f}] "
                  f"y[{mb[1]:.1f},{mb[4]:.1f}] z[{mb[2]:.1f},{mb[5]:.1f}], world centre ({wc[0]:.1f}, {wc[1]:.1f}, {wc[2]:.1f})")

    # ---- pack ----------------------------------------------------------------------
    def write(self, path: str, camera):
        m = self.m
        blobs = [(TAG_BASE & GUEST_MASK, m.data[m.tag_offset:m.tag_offset + m.tag_size])] + self.blobs
        blobs.append((TEX_STRUCT_BASE, bytes(self.tex_structs)))
        header = struct.pack("<4sII", b"XVSC", 3, len(blobs))
        # blob table needs final offsets: header + tables + camera, then blob bytes
        table_size = 12 + len(blobs) * 12 + 4 + len(self.draws) * (24 + 48 + 8) + 9 * 4
        off = table_size
        table = b""
        for addr, data in blobs:
            table += struct.pack("<III", addr, len(data), off)
            off += len(data)
        draws = struct.pack("<I", len(self.draws)) + b"".join(struct.pack("<6I12f2f", *d[:6], *d[6], *d[7]) for d in self.draws)
        cam = struct.pack("<9f", *camera)
        with open(path, "wb") as f:
            f.write(header + table + draws + cam)
            for _addr, data in blobs:
                f.write(data)
        total = sum(len(d) for _a, d in blobs)
        print(f"wrote {path}: {len(blobs)} blobs ({total / 1e6:.1f} MB), {len(self.draws)} draws, "
              f"{len(self.textures)} textures; bounds x[{self.bounds[0]:.0f},{self.bounds[3]:.0f}] "
              f"y[{self.bounds[1]:.0f},{self.bounds[4]:.0f}] z[{self.bounds[2]:.0f},{self.bounds[5]:.0f}]")


def main() -> int:
    ap = argparse.ArgumentParser(prog="recompiler/halo_scene_export.py")
    ap.add_argument("map")
    ap.add_argument("-o", "--output", default="assets/ui_scene.bin")
    ap.add_argument("--models", nargs="*", default=["sky\\sky_ui\\sky_ui", "scenery\\halo\\halo"])
    ap.add_argument("--camera", nargs=9, type=float, metavar="F", default=None,
                    help="eye xyz, target xyz, fov, znear, zfar (default: origin -> ring centre)")
    args = ap.parse_args()
    m = HaloMap(args.map, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".mapcache"))
    ex = SceneExporter(m)
    for name in args.models:
        ex.export_model(name)
    os.makedirs(os.path.dirname(os.path.abspath(args.output)), exist_ok=True)
    cam = args.camera
    if cam is None:
        # Default: eye at the origin (Halo's UI camera lives inside the sky dome), looking
        # along the ring: nearest point of the ring circle + a stretch of its tangent.
        import math
        ring = next((n for n in ex.model_frames if not n.startswith("sky")), None)
        cam = [0, 0, 0, 0, 1, 0, 70, 0.1, 60000]
        if ring:
            world, mb = ex.model_frames[ring]
            R, T = world[:9], world[9:]
            C = ex.model_centres[ring]
            radius = max(mb[3] - mb[0], mb[5] - mb[2]) / 2
            normal = [R[1], R[4], R[7]]                         # model +y (thin axis) in world
            d = [-C[0], -C[1], -C[2]]                           # centre -> origin
            dn = sum(d[i] * normal[i] for i in range(3))
            inplane = [d[i] - dn * normal[i] for i in range(3)]
            l = math.sqrt(sum(x * x for x in inplane)) or 1.0
            P = [C[i] + radius * inplane[i] / l for i in range(3)]       # nearest ring point
            tangent = [normal[1] * (P[2] - C[2]) - normal[2] * (P[1] - C[1]),
                       normal[2] * (P[0] - C[0]) - normal[0] * (P[2] - C[2]),
                       normal[0] * (P[1] - C[1]) - normal[1] * (P[0] - C[0])]
            tl = math.sqrt(sum(x * x for x in tangent)) or 1.0
            # eye: off the band toward the ring centre and above the ring plane; look down the arc
            u = [(P[i] - C[i]) / radius for i in range(3)]
            eye = [C[i] + 0.55 * radius * u[i] + 9.0 * normal[i] for i in range(3)]
            target = [P[i] + 22.0 * tangent[i] / tl - 4.0 * normal[i] for i in range(3)]
            cam = [eye[0], eye[1], eye[2], target[0], target[1], target[2], 65, 0.1, 60000]
            print(f"camera: ring centre {[round(c,1) for c in C]} radius {radius:.1f}, eye "
                  f"{[round(x,1) for x in eye]} -> target {[round(x,1) for x in target]}")
    ex.write(args.output, cam)
    return 0


if __name__ == "__main__":
    sys.exit(main())
