#!/usr/bin/env python3
"""
dx8_shader_parse.py — Stage 2 of the Xita offline static-recompilation pipeline.

Decodes the two DWORD arrays an original-Xbox title hands to
IDirect3DDevice8::CreateVertexShader(pDeclaration, pFunction, ...):

  * pDeclaration — D3DVSD_* vertex-declaration tokens (stream / register /
    skip / constant-preload / tessellator / end).  Also synthesised from a
    plain FVF code, since SetVertexShader() accepts either.
  * pFunction    — Xbox vertex-shader function blob: a 4-byte header followed
    by NV2A vertex-program microcode, one 128-bit (4 x DWORD) instruction per
    slot.  Each slot can dual-issue one MAC op and one ILU op.

Output is a human-readable listing (and optionally JSON) in which every
instruction is fully resolved — opcode, destination register(s) + write mask,
each source register with negate/swizzle, constant addressing — so the next
stage can map lines 1:1 onto Cg statements for psp2cgc/GXP compilation.

Input addressing
----------------
Blobs are read from FILE at a location given as either
    0x1234            a raw file offset, or
    va:0x00031400     a virtual address, translated through the Stage 1 JSON
                      manifest (--manifest game.json) or, when FILE is itself
                      an .xbe and xbe_parse.py sits next to this script, by
                      parsing the section table directly.

Usage
-----
    dx8_shader_parse.py FILE --decl 0x2000 --func 0x2040
    dx8_shader_parse.py FILE --decl va:0x31000 --manifest game.json      # auto-locate func after decl
    dx8_shader_parse.py FILE --func va:0x31400 --manifest game.json --json
    dx8_shader_parse.py --fvf 0x1C2                                      # decl from an FVF code
    dx8_shader_parse.py GAME.xbe --scan [--manifest game.json]           # find candidate shader blobs

Only the Python standard library is used.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional, Tuple

# ============================================================================
# Constants — vertex declaration tokens (D3DVSD_*)
# ============================================================================

VSD_TOKEN_SHIFT = 29
VSD_TOKEN_NOP = 0
VSD_TOKEN_STREAM = 1
VSD_TOKEN_STREAMDATA = 2
VSD_TOKEN_TESSELLATOR = 3
VSD_TOKEN_CONSTMEM = 4
VSD_TOKEN_EXT = 5
VSD_TOKEN_END = 7
VSD_END = 0xFFFFFFFF

VSD_TOKEN_NAMES = {
    VSD_TOKEN_NOP: "NOP", VSD_TOKEN_STREAM: "STREAM", VSD_TOKEN_STREAMDATA: "STREAMDATA",
    VSD_TOKEN_TESSELLATOR: "TESSELLATOR", VSD_TOKEN_CONSTMEM: "CONSTMEM",
    VSD_TOKEN_EXT: "EXT", 6: "RESERVED6", VSD_TOKEN_END: "END",
}

VSD_MASK_TESSSTREAM = 0x10000000   # on a STREAM token: this stream feeds the tessellator
VSD_MASK_SKIP = 0x10000000         # on a STREAMDATA token: skip N dwords
VSD_MASK_SKIPBYTES = 0x08000000    # on a STREAMDATA token: skip N bytes (Xbox extension)
VSD_MASK_TESSUV = 0x10000000       # on a TESSELLATOR token: 0 = TESSNORMAL, 1 = TESSUV

# Xbox vertex data types (X_D3DVSDT_*).  High nibble = component count,
# low nibble = NV2A attribute type (0 UB_D3D, 1 S1, 2 F, 4 UB_OGL, 5 S32K, 6 CMP).
#   name           : (value, byte size, gxm hint, cg type)
VSDT = {
    0x12: ("FLOAT1",      4,  "F32 x1",          "float"),
    0x22: ("FLOAT2",      8,  "F32 x2",          "float2"),
    0x32: ("FLOAT3",      12, "F32 x3",          "float3"),
    0x42: ("FLOAT4",      16, "F32 x4",          "float4"),
    0x40: ("D3DCOLOR",    4,  "U8N x4 (BGRA)",   "float4 /*.bgra*/"),
    0x25: ("SHORT2",      4,  "S16 x2",          "float2"),
    0x45: ("SHORT4",      8,  "S16 x4",          "float4"),
    0x11: ("NORMSHORT1",  2,  "S16N x1",         "float"),
    0x21: ("NORMSHORT2",  4,  "S16N x2",         "float2"),
    0x31: ("NORMSHORT3",  6,  "S16N x3",         "float3"),
    0x41: ("NORMSHORT4",  8,  "S16N x4",         "float4"),
    0x16: ("NORMPACKED3", 4,  "U8 x4 -> unpack 11:11:10 in VS", "float3"),
    0x72: ("FLOAT2H",     12, "F32 x3 (x, y, w; z=0)", "float4"),
    0x14: ("PBYTE1",      1,  "U8N x1",          "float"),
    0x24: ("PBYTE2",      2,  "U8N x2",          "float2"),
    0x34: ("PBYTE3",      3,  "U8N x3",          "float3"),
    0x44: ("PBYTE4",      4,  "U8N x4",          "float4"),
    0x02: ("NONE",        0,  "(no data)",       "-"),
}

# Any code of the form (count << 4) | nv2a_type is well-formed even when the XDK
# headers give it no name (Halo uses 0x15 = S32K x1 and 0x34 = UB_OGL x3).
_NV2A_ATTR_TYPES = {
    0: ("UB_D3D", 1, "U8N"), 1: ("S1", 2, "S16N"), 2: ("F", 4, "F32"),
    4: ("UB_OGL", 1, "U8N"), 5: ("S32K", 2, "S16"), 6: ("CMP", 4, "U8 x4 -> unpack in VS"),
}


def vsdt_info(code: int):
    """(name, byte size, gxm hint, cg type) for a D3DVSDT code, or None if malformed."""
    if code in VSDT:
        return VSDT[code]
    count, t = code >> 4, code & 0xF
    if t not in _NV2A_ATTR_TYPES or not (1 <= count <= 4):
        return None
    tname, unit, gxm = _NV2A_ATTR_TYPES[t]
    size = 4 if t == 6 else unit * count
    cg = "float" if count == 1 else f"float{count}"
    return (f"{tname}x{count}", size, f"{gxm} x{count}", cg)

# Vertex input register conventions (v0..v15) — Xbox D3D fixed assignments.
VREG_SEMANTIC = {
    0: "POSITION", 1: "BLENDWEIGHT", 2: "NORMAL", 3: "COLOR0 (diffuse)",
    4: "COLOR1 (specular)", 5: "FOG", 6: "PSIZE", 7: "BACKCOLOR0",
    8: "BACKCOLOR1", 9: "TEXCOORD0", 10: "TEXCOORD1", 11: "TEXCOORD2",
    12: "TEXCOORD3", 13: "v13 (reserved)", 14: "v14 (reserved)", 15: "v15 (reserved)",
}

# FVF bits (identical to PC DX8 in the fields we need).
FVF_POSITION_MASK = 0x00E
FVF_XYZ, FVF_XYZRHW = 0x002, 0x004
FVF_XYZB1, FVF_XYZB2, FVF_XYZB3, FVF_XYZB4 = 0x006, 0x008, 0x00A, 0x00C
FVF_NORMAL, FVF_PSIZE, FVF_DIFFUSE, FVF_SPECULAR = 0x010, 0x020, 0x040, 0x080
FVF_TEXCOUNT_MASK, FVF_TEXCOUNT_SHIFT = 0xF00, 8
FVF_TEXSIZE_TO_TYPE = {0: 0x22, 1: 0x32, 2: 0x42, 3: 0x12}   # 2D, 3D, 4D, 1D

# ============================================================================
# Constants — NV2A vertex program microcode
# ============================================================================

FUNC_HEADER_FMT = "<BBBB"          # Type, Version, NumInst, Reserved
FUNC_HEADER_SIZE = 4
INSTR_SIZE = 16                    # 4 x DWORD
MAX_INSTRUCTIONS = 136             # NV2A program store

# Header byte 0 is the ASCII magic 'x' (0x78); byte 1 is the kind: ' ' plain
# vertex shader, 's' vertex state shader, 'w' read/write shader.  Verified
# against XDK-3925-era output (Halo: 67 blobs, all "78 20 NN 00").
FUNC_MAGIC = 0x78
XVS_VERSIONS = {0x20: "XVS (vertex shader)", 0x73: "XVSS (vertex state shader)",
                0x77: "XVSW (vertex read/write shader)"}

# Bit-field map: name -> (dword index, lsb, width).  DWORD 0 carries no fields.
FIELDS = {
    "ILU":          (1, 25, 3),
    "MAC":          (1, 21, 4),
    "CONST":        (1, 13, 8),
    "V":            (1,  9, 4),
    "A_NEG":        (1,  8, 1),
    "A_SWZ_X":      (1,  6, 2), "A_SWZ_Y": (1, 4, 2), "A_SWZ_Z": (1, 2, 2), "A_SWZ_W": (1, 0, 2),
    "A_R":          (2, 28, 4),
    "A_MUX":        (2, 26, 2),
    "B_NEG":        (2, 25, 1),
    "B_SWZ_X":      (2, 23, 2), "B_SWZ_Y": (2, 21, 2), "B_SWZ_Z": (2, 19, 2), "B_SWZ_W": (2, 17, 2),
    "B_R":          (2, 13, 4),
    "B_MUX":        (2, 11, 2),
    "C_NEG":        (2, 10, 1),
    "C_SWZ_X":      (2,  8, 2), "C_SWZ_Y": (2, 6, 2), "C_SWZ_Z": (2, 4, 2), "C_SWZ_W": (2, 2, 2),
    "C_R_HIGH":     (2,  0, 2),
    "C_R_LOW":      (3, 30, 2),
    "C_MUX":        (3, 28, 2),
    "OUT_MAC_MASK": (3, 24, 4),
    "OUT_R":        (3, 20, 4),
    "OUT_ILU_MASK": (3, 16, 4),
    "OUT_O_MASK":   (3, 12, 4),
    "OUT_ORB":      (3, 11, 1),
    "OUT_ADDRESS":  (3,  3, 8),
    "OUT_MUX":      (3,  2, 1),
    "A0X":          (3,  1, 1),
    "FINAL":        (3,  0, 1),
}

MAC_OPS = {0: "nop", 1: "mov", 2: "mul", 3: "add", 4: "mad", 5: "dp3", 6: "dph", 7: "dp4",
           8: "dst", 9: "min", 10: "max", 11: "slt", 12: "sge", 13: "arl"}
ILU_OPS = {0: "nop", 1: "mov", 2: "rcp", 3: "rcc", 4: "rsq", 5: "exp", 6: "log", 7: "lit"}

# Which of the three input slots (A, B, C) each MAC opcode consumes.  Note the
# NV2A quirk: ADD reads A and C, not A and B.  All ILU ops read C only.
MAC_INPUTS = {
    "nop": "", "mov": "A", "mul": "AB", "add": "AC", "mad": "ABC", "dp3": "AB", "dph": "AB",
    "dp4": "AB", "dst": "AB", "min": "AB", "max": "AB", "slt": "AB", "sge": "AB", "arl": "A",
}

PARAM_R, PARAM_V, PARAM_C = 1, 2, 3          # input MUX values (0 = unused)
OUTPUT_C, OUTPUT_O = 0, 1                    # OUT_ORB
OMUX_MAC, OMUX_ILU = 0, 1                    # OUT_MUX
SWZ = "xyzw"
MASK_BITS = ((0x8, "x"), (0x4, "y"), (0x2, "z"), (0x1, "w"))

OREG = {0: "oPos", 3: "oD0", 4: "oD1", 5: "oFog", 6: "oPts", 7: "oB0", 8: "oB1",
        9: "oT0", 10: "oT1", 11: "oT2", 12: "oT3"}
OREG_CG = {"oPos": "POSITION", "oD0": "COLOR0", "oD1": "COLOR1", "oFog": "FOG", "oPts": "PSIZE",
           "oB0": "BCOL0 (back diffuse)", "oB1": "BCOL1 (back specular)",
           "oT0": "TEXCOORD0", "oT1": "TEXCOORD1", "oT2": "TEXCOORD2", "oT3": "TEXCOORD3"}

# Cg mapping hints emitted in the listing header.  ILU ops are scalar: they
# read the FIRST component of the swizzled operand and broadcast the result.
CG_HINTS = {
    "mov": "d = a",
    "mul": "d = a * b",
    "add": "d = a + c                     (NV2A add reads slots A and C)",
    "mad": "d = a * b + c",
    "dp3": "d = dot(a.xyz, b.xyz).xxxx",
    "dph": "d = (dot(a.xyz, b.xyz) + b.w).xxxx",
    "dp4": "d = dot(a, b).xxxx",
    "dst": "d = float4(1, a.y*b.y, a.z, b.w)",
    "min": "d = min(a, b)",
    "max": "d = max(a, b)",
    "slt": "d = (a < b)  ? 1 : 0   per component",
    "sge": "d = (a >= b) ? 1 : 0   per component",
    "arl": "a0.x = (int)floor(a.x)",
    "rcp": "d = (1 / c.x).xxxx",
    "rcc": "d = clamp(1 / c.x, 5.42e-20, 1.84e19).xxxx  (sign preserved)",
    "rsq": "d = (1 / sqrt(abs(c.x))).xxxx",
    "exp": "d = float4(2^floor(c.x), frac(c.x), 2^c.x, 1)",
    "log": "d = float4(exponent(c.x), mantissa(c.x), log2(abs(c.x)), 1)",
    "lit": "d = float4(1, max(c.x,0), (c.x>0) ? pow(max(c.y,0), clamp(c.w,-128,128)) : 0, 1)",
}

DEFAULT_CONST_BIAS = 96   # D3D c0 == hardware c[96] unless D3DRS_VERTEXSHADERCONSTANTMODE = 192CONSTANTS


class ShaderError(Exception):
    pass


# ============================================================================
# Address resolution (file offset / VA via manifest / VA via xbe_parse)
# ============================================================================

class AddressSpace:
    """Translates 'va:0x...' specs to file offsets using a Stage 1 manifest."""

    def __init__(self, manifest: Optional[dict]):
        self.base = self.headers = 0
        self.sections: List[Tuple[int, int, int]] = []     # (va, raw_off, raw_size)
        if manifest:
            self.base = manifest["base_address"]
            self.headers = manifest["size_of_headers"]
            self.sections = [(s["virtual_address"], s["raw_address"], s["raw_size"])
                             for s in manifest["sections"]]

    @classmethod
    def from_file(cls, path: str, manifest_path: Optional[str]) -> "AddressSpace":
        if manifest_path:
            with open(manifest_path, "r", encoding="utf-8") as f:
                return cls(json.load(f))
        # No manifest: if FILE is an XBE and xbe_parse.py is beside us, use it.
        try:
            with open(path, "rb") as f:
                magic = f.read(4)
        except OSError:
            magic = b""
        if magic == b"XBEH":
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            try:
                import xbe_parse  # type: ignore
                with open(path, "rb") as f:
                    info = xbe_parse.XbeParser(f.read(), path).parse()
                return cls(json.loads(xbe_parse.to_json(info)))
            except ImportError:
                pass
        return cls(None)

    def va_to_offset(self, va: int) -> int:
        if not self.sections and not self.headers:
            raise ShaderError(f"cannot translate VA 0x{va:08X}: no manifest (use --manifest game.json)")
        if self.base <= va < self.base + self.headers:
            return va - self.base
        for sva, roff, rsize in self.sections:
            if sva <= va < sva + rsize:
                return roff + (va - sva)
        raise ShaderError(f"VA 0x{va:08X} is not backed by file data in any section")

    def resolve(self, spec: str) -> int:
        spec = spec.strip()
        if spec.lower().startswith("va:"):
            return self.va_to_offset(int(spec[3:], 0))
        return int(spec, 0)

    def offset_to_va(self, off: int) -> Optional[int]:
        if not self.sections and not self.headers:
            return None
        if off < self.headers:
            return self.base + off
        for sva, roff, rsize in self.sections:
            if roff <= off < roff + rsize:
                return sva + (off - roff)
        return None


# ============================================================================
# Vertex declaration decoding
# ============================================================================

@dataclass
class DeclToken:
    index: int
    raw: int
    kind: str
    text: str
    stream: Optional[int] = None
    vreg: Optional[int] = None
    dtype: Optional[str] = None
    dtype_code: Optional[int] = None
    offset: Optional[int] = None
    size: Optional[int] = None
    extra: List[float] = field(default_factory=list)


@dataclass
class Declaration:
    file_offset: Optional[int]
    token_count: int
    byte_size: int
    tokens: List[DeclToken]
    streams: Dict[int, int]                       # stream -> stride
    attributes: List[Dict]                        # flattened v-register layout for Stage 3
    warnings: List[str]


def decode_declaration(data: bytes, off: int = 0, max_tokens: int = 256) -> Declaration:
    tokens: List[DeclToken] = []
    warnings: List[str] = []
    streams: Dict[int, int] = {}
    attrs: List[Dict] = []
    stream = -1
    cursor = 0
    pos = off
    i = 0
    ended = False

    def u32(p: int) -> int:
        if p + 4 > len(data):
            raise ShaderError(f"declaration runs past end of data at 0x{p:X} (no D3DVSD_END found)")
        return struct.unpack_from("<I", data, p)[0]

    while i < max_tokens:
        raw = u32(pos)
        ttype = raw >> VSD_TOKEN_SHIFT
        tok = DeclToken(index=i, raw=raw, kind=VSD_TOKEN_NAMES.get(ttype, f"?{ttype}"), text="")
        consumed = 4

        if raw == VSD_END:
            tok.text = "D3DVSD_END()"
            ended = True
        elif ttype == VSD_TOKEN_NOP:
            tok.text = "D3DVSD_NOP()"
        elif ttype == VSD_TOKEN_STREAM:
            stream = raw & 0xF
            cursor = 0
            tess = bool(raw & VSD_MASK_TESSSTREAM)
            tok.stream = stream
            tok.text = f"D3DVSD_STREAM{'_TESS' if tess else ''}({stream})"
            streams.setdefault(stream, 0)
        elif ttype == VSD_TOKEN_STREAMDATA:
            if stream < 0:
                warnings.append(f"token {i}: STREAMDATA before any STREAM token; assuming stream 0")
                stream = 0
                streams.setdefault(0, 0)
            if raw & VSD_MASK_SKIPBYTES:
                n = (raw >> 16) & 0xFF
                tok.text = f"D3DVSD_SKIPBYTES({n})"
                tok.stream, tok.offset, tok.size = stream, cursor, n
                cursor += n
            elif raw & VSD_MASK_SKIP:
                n = (raw >> 16) & 0xF
                tok.text = f"D3DVSD_SKIP({n})            ; {n * 4} bytes"
                tok.stream, tok.offset, tok.size = stream, cursor, n * 4
                cursor += n * 4
            else:
                dtype = (raw >> 16) & 0xFF
                vreg = raw & 0xF
                info = vsdt_info(dtype)
                if info is None:
                    info = (f"UNKNOWN_0x{dtype:02X}", 0, "?", "?")
                    warnings.append(f"token {i}: malformed data type 0x{dtype:02X} for v{vreg}; size assumed 0")
                name, size, gxm, _cg = info
                tok.stream, tok.vreg, tok.dtype, tok.dtype_code = stream, vreg, name, dtype
                tok.offset, tok.size = cursor, size
                tok.text = (f"D3DVSD_REG(v{vreg:<2}, D3DVSDT_{name:<11})  "
                            f"; stream {stream} +{cursor:<3} {size:>2} B  {VREG_SEMANTIC.get(vreg, '')}  -> {gxm}")
                attrs.append({"vreg": vreg, "semantic": VREG_SEMANTIC.get(vreg, ""), "stream": stream,
                              "offset": cursor, "size": size, "type": name, "type_code": dtype, "gxm": gxm})
                cursor += size
            streams[stream] = cursor
        elif ttype == VSD_TOKEN_TESSELLATOR:
            uv = bool(raw & VSD_MASK_TESSUV)
            vout = raw & 0xF
            vin = (raw >> 20) & 0xF
            dtype = (raw >> 16) & 0xF
            tok.vreg = vout
            tok.text = (f"D3DVSD_TESSUV(v{vout})" if uv
                        else f"D3DVSD_TESSNORMAL(v{vin} -> v{vout})") + f"  ; type nibble {dtype}"
        elif ttype == VSD_TOKEN_CONSTMEM:
            count = (raw >> 25) & 0xF
            addr = raw & 0xFF
            floats = []
            for k in range(count * 4):
                floats.append(struct.unpack_from("<f", data, pos + 4 + k * 4)[0])
            consumed += count * 16
            tok.extra = floats
            rows = ", ".join("(" + ", ".join(f"{v:g}" for v in floats[j:j + 4]) + ")"
                             for j in range(0, len(floats), 4))
            tok.text = f"D3DVSD_CONST(c{addr}, {count})  ; {rows}"
        elif ttype == VSD_TOKEN_EXT:
            count = (raw >> 24) & 0x1F
            info = raw & 0x1F
            consumed += count * 4
            tok.text = f"D3DVSD_EXT(info={info}, count={count})  ; {count} payload dwords skipped"
        else:
            tok.text = f"<unknown token type {ttype}>"
            warnings.append(f"token {i}: unknown token type {ttype} (raw 0x{raw:08X})")

        tokens.append(tok)
        pos += consumed
        i += 1
        if ended:
            break

    if not ended:
        raise ShaderError(f"no D3DVSD_END within {max_tokens} tokens starting at 0x{off:X}")

    return Declaration(file_offset=off, token_count=len(tokens), byte_size=pos - off,
                       tokens=tokens, streams=streams, attributes=attrs, warnings=warnings)


def fvf_to_declaration_tokens(fvf: int) -> Tuple[List[int], List[str]]:
    """Synthesise the D3DVSD token array Xbox D3D builds for a plain FVF code."""
    notes: List[str] = []
    toks = [VSD_TOKEN_STREAM << VSD_TOKEN_SHIFT | 0]

    def reg(v: int, t: int) -> int:
        return (VSD_TOKEN_STREAMDATA << VSD_TOKEN_SHIFT) | (t << 16) | v

    pos = fvf & FVF_POSITION_MASK
    if pos == FVF_XYZRHW:
        toks.append(reg(0, 0x42))
    elif pos in (FVF_XYZ, FVF_XYZB1, FVF_XYZB2, FVF_XYZB3, FVF_XYZB4):
        toks.append(reg(0, 0x32))
        nb = {FVF_XYZ: 0, FVF_XYZB1: 1, FVF_XYZB2: 2, FVF_XYZB3: 3, FVF_XYZB4: 4}[pos]
        if nb:
            toks.append(reg(1, {1: 0x12, 2: 0x22, 3: 0x32, 4: 0x42}[nb]))
    elif pos:
        notes.append(f"unsupported position type 0x{pos:X}")
    if fvf & FVF_NORMAL:
        toks.append(reg(2, 0x32))
    if fvf & FVF_PSIZE:
        toks.append(reg(6, 0x12))
        notes.append("bit 0x020 treated as PSIZE -> v6 FLOAT1 (reserved on some XDK versions)")
    if fvf & FVF_DIFFUSE:
        toks.append(reg(3, 0x40))
    if fvf & FVF_SPECULAR:
        toks.append(reg(4, 0x40))
    ntex = (fvf & FVF_TEXCOUNT_MASK) >> FVF_TEXCOUNT_SHIFT
    for t in range(min(ntex, 4)):
        size_bits = (fvf >> (16 + 2 * t)) & 0x3
        toks.append(reg(9 + t, FVF_TEXSIZE_TO_TYPE[size_bits]))
    if ntex > 4:
        notes.append(f"FVF requests {ntex} texcoord sets; Xbox supports 4 (v9..v12) — truncated")
    toks.append(VSD_END)
    return toks, notes


# ============================================================================
# NV2A microcode decoding
# ============================================================================

def get_field(words: Tuple[int, int, int, int], name: str) -> int:
    dw, lsb, width = FIELDS[name]
    return (words[dw] >> lsb) & ((1 << width) - 1)


def swizzle_str(sx: int, sy: int, sz: int, sw: int) -> str:
    s = SWZ[sx] + SWZ[sy] + SWZ[sz] + SWZ[sw]
    if s == "xyzw":
        return ""
    if s[0] * 4 == s:
        return "." + s[0]
    return "." + s


def mask_str(mask: int) -> str:
    if mask == 0xF:
        return ""
    return "." + "".join(ch for bit, ch in MASK_BITS if mask & bit)


@dataclass
class Operand:
    slot: str                   # "A" | "B" | "C"
    kind: str                   # "r" | "v" | "c" | "unused"
    index: int
    negate: bool
    swizzle: str                # 4-char swizzle
    relative: bool              # constant indexed by a0.x
    text: str
    d3d_index: Optional[int] = None    # constant index in D3D (SetVertexShaderConstant) numbering


@dataclass
class Output:
    kind: str                   # "r" | "o" | "c" | "a0"
    index: int
    name: str
    mask: int
    text: str
    cg_semantic: Optional[str] = None


@dataclass
class Op:
    unit: str                   # "MAC" | "ILU"
    opcode: str
    outputs: List[Output]
    inputs: List[Operand]
    text: str
    cg_hint: str


@dataclass
class Instruction:
    slot: int
    file_offset: int
    words: List[int]
    final: bool
    ops: List[Op]
    warnings: List[str]


@dataclass
class ShaderFunction:
    file_offset: int
    type_code: int
    type_name: str
    version_code: int
    version_name: str
    declared_count: int
    decoded_count: int
    byte_size: int
    const_bias: int
    instructions: List[Instruction]
    inputs_used: List[str]
    outputs_used: List[str]
    temps_used: List[str]
    constants_used: List[int]
    warnings: List[str]


def _operand(words, slot: str, v_index: int, c_index: int, a0x: bool, const_bias: int) -> Operand:
    mux = get_field(words, f"{slot}_MUX")
    neg = bool(get_field(words, f"{slot}_NEG"))
    sw = tuple(get_field(words, f"{slot}_SWZ_{c}") for c in "XYZW")
    swz = SWZ[sw[0]] + SWZ[sw[1]] + SWZ[sw[2]] + SWZ[sw[3]]
    if slot == "C":
        r_index = (get_field(words, "C_R_HIGH") << 2) | get_field(words, "C_R_LOW")
    else:
        r_index = get_field(words, f"{slot}_R")

    rel = False
    d3d = None
    if mux == PARAM_R:
        kind, idx, base = "r", r_index, f"r{r_index}"
        if r_index == 12:
            base = "r12"  # readable alias of the oPos output register
    elif mux == PARAM_V:
        kind, idx, base = "v", v_index, f"v{v_index}"
    elif mux == PARAM_C:
        kind, idx = "c", c_index
        rel = a0x
        d3d = c_index - const_bias
        base = f"c[{'a0.x+' if rel else ''}{c_index}]"
    else:
        kind, idx, base = "unused", 0, "?"
    text = ("-" if neg else "") + base + swizzle_str(*sw)
    return Operand(slot=slot, kind=kind, index=idx, negate=neg, swizzle=swz, relative=rel,
                   text=text, d3d_index=d3d)


def decode_instruction(words: Tuple[int, int, int, int], slot: int, file_offset: int,
                       const_bias: int) -> Instruction:
    warnings: List[str] = []
    mac = MAC_OPS.get(get_field(words, "MAC"), "?")
    ilu = ILU_OPS.get(get_field(words, "ILU"), "?")
    v_index = get_field(words, "V")
    c_index = get_field(words, "CONST")
    a0x = bool(get_field(words, "A0X"))
    final = bool(get_field(words, "FINAL"))

    operands = {s: _operand(words, s, v_index, c_index, a0x, const_bias) for s in "ABC"}

    out_mac_mask = get_field(words, "OUT_MAC_MASK")
    out_r = get_field(words, "OUT_R")
    out_ilu_mask = get_field(words, "OUT_ILU_MASK")
    out_o_mask = get_field(words, "OUT_O_MASK")
    out_orb = get_field(words, "OUT_ORB")
    out_addr = get_field(words, "OUT_ADDRESS")
    out_mux = get_field(words, "OUT_MUX")

    def external_output() -> Optional[Output]:
        """The o-register / constant write, shared by MAC and ILU via OUT_MUX."""
        if not out_o_mask:
            return None
        if out_orb == OUTPUT_O:
            idx = out_addr & 0xF
            name = OREG.get(idx)
            if name is None:
                warnings.append(f"write to undefined output register o{idx}")
                name = f"o?{idx}"
            return Output(kind="o", index=idx, name=name, mask=out_o_mask,
                          text=name + mask_str(out_o_mask), cg_semantic=OREG_CG.get(name))
        return Output(kind="c", index=out_addr, name=f"c[{out_addr}]", mask=out_o_mask,
                      text=f"c[{out_addr}]" + mask_str(out_o_mask))

    ops: List[Op] = []
    if mac != "nop":
        outs: List[Output] = []
        ins = [operands[s] for s in MAC_INPUTS.get(mac, "")]
        if mac == "arl":
            outs.append(Output(kind="a0", index=0, name="a0.x", mask=0x8, text="a0.x"))
        else:
            if out_mac_mask:
                outs.append(Output(kind="r", index=out_r, name=f"r{out_r}", mask=out_mac_mask,
                                   text=f"r{out_r}" + mask_str(out_mac_mask)))
            if out_mux == OMUX_MAC:
                ext = external_output()
                if ext:
                    outs.append(ext)
            if not outs:
                warnings.append(f"MAC {mac} has no destination (result discarded)")
        for o in ins:
            if o.kind == "unused":
                warnings.append(f"MAC {mac} reads slot {o.slot} but its MUX is 0 (unused)")
        ops.append(Op(unit="MAC", opcode=mac, outputs=outs, inputs=ins,
                      text=_fmt_op(mac, outs, ins), cg_hint=CG_HINTS.get(mac, "")))

    if ilu != "nop":
        outs = []
        ins = [operands["C"]]
        if out_ilu_mask:
            # The ILU's temporary write port is hard-wired to r1.
            outs.append(Output(kind="r", index=1, name="r1", mask=out_ilu_mask,
                               text="r1" + mask_str(out_ilu_mask)))
        if out_mux == OMUX_ILU:
            ext = external_output()
            if ext:
                outs.append(ext)
        if not outs:
            warnings.append(f"ILU {ilu} has no destination (result discarded)")
        if ins[0].kind == "unused":
            warnings.append(f"ILU {ilu} reads slot C but its MUX is 0 (unused)")
        ops.append(Op(unit="ILU", opcode=ilu, outputs=outs, inputs=ins,
                      text=_fmt_op(ilu, outs, ins), cg_hint=CG_HINTS.get(ilu, "")))

    if not ops:
        ops.append(Op(unit="MAC", opcode="nop", outputs=[], inputs=[], text="nop", cg_hint=""))

    return Instruction(slot=slot, file_offset=file_offset, words=list(words), final=final,
                       ops=ops, warnings=warnings)


def _fmt_op(opcode: str, outs: List[Output], ins: List[Operand]) -> str:
    dst = " & ".join(o.text for o in outs) if outs else "(none)"
    src = ", ".join(i.text for i in ins)
    return f"{opcode} {dst}" + (f", {src}" if src else "")


def looks_like_function(data: bytes, off: int) -> Optional[int]:
    """Cheap validator used by --scan and auto-locate.  Returns byte size or None."""
    if off + FUNC_HEADER_SIZE > len(data):
        return None
    t, ver, n, res = struct.unpack_from(FUNC_HEADER_FMT, data, off)
    if t != FUNC_MAGIC or ver not in XVS_VERSIONS or not (1 <= n <= MAX_INSTRUCTIONS) or res != 0:
        return None
    size = FUNC_HEADER_SIZE + n * INSTR_SIZE
    if off + size > len(data):
        return None
    for i in range(n):
        w = struct.unpack_from("<4I", data, off + FUNC_HEADER_SIZE + i * INSTR_SIZE)
        if w[0] != 0:                       # DWORD 0 carries no fields; assemblers emit 0
            return None
        is_final = bool(get_field(w, "FINAL"))
        if is_final != (i == n - 1):        # FINAL exactly on the last slot
            return None
        if get_field(w, "MAC") > 13:        # undefined MAC opcode
            return None
    return size


def decode_function(data: bytes, off: int, const_bias: int = DEFAULT_CONST_BIAS) -> ShaderFunction:
    if off + FUNC_HEADER_SIZE > len(data):
        raise ShaderError(f"function header at 0x{off:X} runs past end of data")
    t, ver, n, res = struct.unpack_from(FUNC_HEADER_FMT, data, off)
    warnings: List[str] = []
    if t != FUNC_MAGIC:
        warnings.append(f"header magic is 0x{t:02X}, expected 0x78 ('x')")
    if ver not in XVS_VERSIONS:
        warnings.append(f"unknown shader Version 0x{ver:02X} (expected 0x20 / 0x73 / 0x77)")
    if res:
        warnings.append(f"header reserved byte is 0x{res:02X}, expected 0")
    if n == 0 or n > MAX_INSTRUCTIONS:
        warnings.append(f"NumInst {n} outside 1..{MAX_INSTRUCTIONS}")

    instrs: List[Instruction] = []
    pos = off + FUNC_HEADER_SIZE
    # Decode up to the declared count (or the hardware maximum if the header
    # says 0); stop early at the FINAL flag either way.
    for i in range(n or MAX_INSTRUCTIONS):
        if pos + INSTR_SIZE > len(data):
            warnings.append(f"data ends after {i} of {n} declared instructions")
            break
        words = struct.unpack_from("<4I", data, pos)
        ins = decode_instruction(words, i, pos, const_bias)
        instrs.append(ins)
        pos += INSTR_SIZE
        if ins.final:
            if i != n - 1:
                warnings.append(f"FINAL flag on slot {i} but header declares {n} instructions")
            break
    else:
        if instrs and not instrs[-1].final:
            warnings.append("last decoded instruction lacks the FINAL flag")

    # Usage summary — this is what Stage 3 needs to build the GXM attribute
    # layout, the uniform block and the varying set.
    vin, vout, temps, consts = set(), set(), set(), set()
    for ins in instrs:
        for op in ins.ops:
            for o in op.inputs:
                if o.kind == "v":
                    vin.add(o.index)
                elif o.kind == "r":
                    temps.add(o.index)
                elif o.kind == "c":
                    consts.add(o.index)
            for o in op.outputs:
                if o.kind == "o":
                    vout.add(o.name)
                elif o.kind == "r":
                    temps.add(o.index)
                elif o.kind == "c":
                    consts.add(o.index)
        warnings.extend(f"slot {ins.slot}: {w}" for w in ins.warnings)
    if "oPos" not in vout and t == 1:
        warnings.append("vertex shader never writes oPos")

    return ShaderFunction(
        file_offset=off, type_code=t, type_name=("'x' XDK shader blob" if t == FUNC_MAGIC else "unknown magic"),
        version_code=ver, version_name=XVS_VERSIONS.get(ver, "?"),
        declared_count=n, decoded_count=len(instrs), byte_size=pos - off,
        const_bias=const_bias, instructions=instrs,
        inputs_used=[f"v{i}" for i in sorted(vin)],
        outputs_used=sorted(vout, key=lambda s: list(OREG.values()).index(s) if s in OREG.values() else 99),
        temps_used=[f"r{i}" for i in sorted(temps)],
        constants_used=sorted(consts),
        warnings=warnings,
    )


def locate_function_after(data: bytes, start: int, window: int = 256) -> Optional[int]:
    """Search forward (4-byte aligned) from `start` for a plausible function header."""
    p = (start + 3) & ~3
    while p < min(len(data), start + window):
        if looks_like_function(data, p):
            return p
        p += 4
    return None


# ============================================================================
# Scanning an image for shader blobs
# ============================================================================

def scan_image(data: bytes, aspace: AddressSpace) -> Dict[str, List[Dict]]:
    funcs, decls = [], []
    p = 0
    n = len(data)
    while p + FUNC_HEADER_SIZE + INSTR_SIZE <= n:
        size = looks_like_function(data, p)
        if size:
            t, ver, cnt, _ = struct.unpack_from(FUNC_HEADER_FMT, data, p)
            funcs.append({"offset": p, "va": aspace.offset_to_va(p), "size": size,
                          "type": XVS_VERSIONS[ver].split(" ")[0], "version": f"0x{ver:02X}",
                          "instructions": cnt})
            p += size
            continue
        # Declaration heuristic: STREAM(0..15) followed by >=1 valid REG tokens and END within 64 tokens.
        w = struct.unpack_from("<I", data, p)[0]
        if (w >> VSD_TOKEN_SHIFT) == VSD_TOKEN_STREAM and (w & ~0x1000000F) == 0x20000000:
            q, regs, ok = p + 4, 0, False
            for _ in range(64):
                if q + 4 > n:
                    break
                tok = struct.unpack_from("<I", data, q)[0]
                tt = tok >> VSD_TOKEN_SHIFT
                if tok == VSD_END:
                    ok = regs > 0
                    break
                if tt == VSD_TOKEN_STREAMDATA:
                    if not (tok & (VSD_MASK_SKIP | VSD_MASK_SKIPBYTES)):
                        if vsdt_info((tok >> 16) & 0xFF) is None or (tok & 0xFFF0) != 0:
                            break
                        regs += 1
                    q += 4
                elif tt == VSD_TOKEN_STREAM and (tok & ~0x1000000F) == 0x20000000:
                    q += 4
                elif tt == VSD_TOKEN_CONSTMEM:
                    q += 4 + ((tok >> 25) & 0xF) * 16
                elif tt == VSD_TOKEN_TESSELLATOR or tt == VSD_TOKEN_NOP:
                    q += 4
                else:
                    break
            if ok:
                decls.append({"offset": p, "va": aspace.offset_to_va(p), "size": q + 4 - p, "registers": regs})
                p = q + 4
                continue
        p += 4
    return {"functions": funcs, "declarations": decls}


# ============================================================================
# Reporting
# ============================================================================

def print_declaration(d: Declaration, aspace: AddressSpace, out=sys.stdout) -> None:
    p = lambda *a: print(*a, file=out)  # noqa: E731
    va = aspace.offset_to_va(d.file_offset) if d.file_offset is not None else None
    loc = f"@ file 0x{d.file_offset:X}" if d.file_offset is not None else "(synthesised)"
    if va is not None:
        loc += f" / VA 0x{va:08X}"
    p("=" * 78)
    p(f"Vertex Declaration {loc}: {d.token_count} tokens, {d.byte_size} bytes")
    p("=" * 78)
    for t in d.tokens:
        p(f"  [{t.index:>2}] 0x{t.raw:08X}  {t.text}")
    p("")
    for s in sorted(d.streams):
        p(f"  stream {s}: stride {d.streams[s]} bytes")
    if d.attributes:
        p("")
        p("  GXM attribute layout (Stage 3 input):")
        for a in d.attributes:
            p(f"    v{a['vreg']:<2} {a['semantic']:<18} stream {a['stream']} offset {a['offset']:>3} "
              f"size {a['size']:>2}  {a['type']:<11} -> {a['gxm']}")
    for w in d.warnings:
        p(f"  ! {w}")
    p("")


def print_function(f: ShaderFunction, aspace: AddressSpace, out=sys.stdout, show_hints: bool = True) -> None:
    p = lambda *a: print(*a, file=out)  # noqa: E731
    va = aspace.offset_to_va(f.file_offset)
    loc = f"@ file 0x{f.file_offset:X}" + (f" / VA 0x{va:08X}" if va is not None else "")
    p("=" * 78)
    p(f"Vertex Shader Function {loc}")
    p("=" * 78)
    p(f"  Type ............ {f.type_code} ({f.type_name})")
    p(f"  Version ......... 0x{f.version_code:02X} ({f.version_name})")
    p(f"  Instructions .... {f.decoded_count} decoded / {f.declared_count} declared  "
      f"({f.byte_size} bytes incl. 4-byte header)")
    p(f"  Constant bias ... c[{f.const_bias}] == D3D c0")
    p(f"  Inputs .......... {' '.join(f.inputs_used) or '-'}")
    p(f"  Outputs ......... {' '.join(f.outputs_used) or '-'}")
    p(f"  Temps ........... {' '.join(f.temps_used) or '-'}")
    if f.constants_used:
        lo, hi = f.constants_used[0], f.constants_used[-1]
        p(f"  Constants ....... c[{lo}]..c[{hi}]  (D3D c{lo - f.const_bias}..c{hi - f.const_bias}), "
          f"{len(f.constants_used)} distinct")
    p("")
    p(f"  {'slot':>4}  {'dword0':<8} {'dword1':<8} {'dword2':<8} {'dword3':<8}  instruction")
    p("  " + "-" * 74)
    for ins in f.instructions:
        w = ins.words
        first = True
        for op in ins.ops:
            prefix = (f"  {ins.slot:>4}  {w[0]:08X} {w[1]:08X} {w[2]:08X} {w[3]:08X}  "
                      if first else f"  {'':>4}  {'':<35}  + ")
            comment = _op_comment(op, f.const_bias)
            p(f"{prefix}{op.text:<40}{('; ' + comment) if comment else ''}")
            first = False
        if ins.final:
            p(f"  {'':>4}  {'':<35}  ; --- FINAL ---")
    if show_hints:
        used = sorted({op.opcode for ins in f.instructions for op in ins.ops if op.opcode != "nop"})
        p("")
        p("  Cg mapping hints for opcodes used:")
        for o in used:
            p(f"    {o:<4} {CG_HINTS.get(o, '')}")
        outs = [o for o in f.outputs_used if o in OREG_CG]
        if outs:
            p("  Output semantics: " + ", ".join(f"{o} -> {OREG_CG[o]}" for o in outs))
        p("  Notes: r12 reads back oPos; ILU ops are scalar (first swizzled component, broadcast);")
        p("         c[a0.x+n] needs a uniform float4 array in Cg; constant index = hardware - bias.")
    if f.warnings:
        p("")
        p(f"  Warnings ({len(f.warnings)}):")
        for w in f.warnings:
            p(f"    ! {w}")
    p("")


def _op_comment(op: Op, bias: int) -> str:
    bits = []
    for o in op.outputs:
        if o.kind == "o" and o.cg_semantic:
            bits.append(f"{o.name}={o.cg_semantic}")
    for i in op.inputs:
        if i.kind == "c" and i.d3d_index is not None:
            bits.append(f"c[{i.index}]=D3D c{i.d3d_index}")
        elif i.kind == "v":
            bits.append(f"v{i.index}={VREG_SEMANTIC.get(i.index, '?').split(' ')[0]}")
        elif i.kind == "r" and i.index == 12:
            bits.append("r12=oPos")
    return " ".join(dict.fromkeys(bits))


# ============================================================================
# CLI
# ============================================================================

def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        prog="dx8_shader_parse.py",
        description="Decode Xbox DX8 vertex declarations and NV2A vertex-shader microcode (Xita Stage 2).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Locations are file offsets (0x1234) or virtual addresses (va:0x31400, needs --manifest\n"
               "or an .xbe FILE with xbe_parse.py alongside).")
    ap.add_argument("file", nargs="?", help="binary containing the token arrays (.xbe or raw dump)")
    ap.add_argument("--decl", metavar="LOC", help="location of the D3DVSD_* declaration tokens")
    ap.add_argument("--func", metavar="LOC", help="location of the shader function blob (header + microcode)")
    ap.add_argument("--fvf", metavar="CODE", help="synthesise the declaration from an FVF code (e.g. 0x1C2)")
    ap.add_argument("--manifest", metavar="JSON", help="Stage 1 xbe_parse.py --json output for VA translation")
    ap.add_argument("--const-bias", type=int, default=DEFAULT_CONST_BIAS,
                    help="hardware index of D3D c0 (96 default; 0 for D3DSCM_192CONSTANTS titles)")
    ap.add_argument("--scan", action="store_true", help="scan FILE for candidate function/declaration blobs")
    ap.add_argument("--json", action="store_true", help="emit JSON instead of the text listing")
    ap.add_argument("--no-hints", action="store_true", help="omit the Cg mapping hint block")
    args = ap.parse_args(argv)

    if not (args.file or args.fvf):
        ap.error("need FILE (with --decl/--func/--scan) or --fvf")

    result: Dict = {}
    data = b""
    aspace = AddressSpace(None)
    if args.file:
        try:
            with open(args.file, "rb") as fh:
                data = fh.read()
        except OSError as e:
            print(f"error: cannot read {args.file}: {e}", file=sys.stderr)
            return 2
        try:
            aspace = AddressSpace.from_file(args.file, args.manifest)
        except (OSError, ValueError, KeyError) as e:
            print(f"error: cannot load manifest: {e}", file=sys.stderr)
            return 2

    try:
        if args.scan:
            if not data:
                ap.error("--scan needs FILE")
            found = scan_image(data, aspace)
            result["scan"] = found
            if not args.json:
                print(f"Scan of {args.file}: {len(found['functions'])} function blob(s), "
                      f"{len(found['declarations'])} declaration(s)")
                for f in found["functions"]:
                    va = f"VA 0x{f['va']:08X}" if f["va"] is not None else ""
                    print(f"  func  @ 0x{f['offset']:08X} {va:<14} {f['type']:<9} ver {f['version']} "
                          f"{f['instructions']:>3} instr  {f['size']:>5} B")
                for d in found["declarations"]:
                    va = f"VA 0x{d['va']:08X}" if d["va"] is not None else ""
                    print(f"  decl  @ 0x{d['offset']:08X} {va:<14} {d['registers']} register(s)  {d['size']:>5} B")
                print("")

        decl = None
        if args.fvf:
            toks, notes = fvf_to_declaration_tokens(int(args.fvf, 0))
            decl = decode_declaration(struct.pack(f"<{len(toks)}I", *toks), 0)
            decl.file_offset = None
            decl.warnings.extend(notes)
        elif args.decl:
            decl = decode_declaration(data, aspace.resolve(args.decl))
        if decl:
            result["declaration"] = asdict(decl)
            if not args.json:
                print_declaration(decl, aspace)

        func_off = None
        if args.func:
            func_off = aspace.resolve(args.func)
        elif decl and decl.file_offset is not None:
            func_off = locate_function_after(data, decl.file_offset + decl.byte_size)
            if func_off is None and not args.json:
                print("  (no function blob found within 256 bytes after the declaration; pass --func)\n")
        if func_off is not None:
            func = decode_function(data, func_off, const_bias=args.const_bias)
            result["function"] = asdict(func)
            if not args.json:
                print_function(func, aspace, show_hints=not args.no_hints)

        if not result:
            ap.error("nothing to do: give --decl and/or --func, --fvf, or --scan")
    except ShaderError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    except struct.error as e:
        print(f"error: truncated data ({e})", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(main())
