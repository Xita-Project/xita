#!/usr/bin/env python3
"""
dx8_pixelshader_parse.py - Stage 2c of the Xita pipeline: decode Xbox pixel
shaders (NV2A register-combiner definitions).

An Xbox pixel shader is not a token stream but a 240-byte D3DPIXELSHADERDEF
struct, produced offline by the XDK assembler from .psh source and handed to
D3DDevice_CreatePixelShader().  It programs the NV2A's 8 general combiner
stages + final combiner + 4 texture-address units directly:

    off  field                      meaning
    0x00 PSAlphaInputs[8]           a,b,c,d inputs of the alpha combiner, per stage
    0x20 PSFinalCombinerInputsABCD  final combiner: rgb = A*B + (1-A)*C + D
    0x24 PSFinalCombinerInputsEFG   E,F (EF_PROD), G (-> alpha), settings byte
    0x28 PSConstant0[8]             default c0 colour per stage (ARGB)
    0x48 PSConstant1[8]             default c1 colour per stage
    0x68 PSAlphaOutputs[8]          alpha combiner outputs: cd | ab<<4 | sum<<8 | flags<<12
    0x88 PSRGBInputs[8]             a,b,c,d inputs of the rgb combiner, per stage
    0xA8 PSCompareMode              4 bits/stage for CLIPPLANE texture mode
    0xAC PSFinalCombinerConstant0   default final-combiner c0
    0xB0 PSFinalCombinerConstant1   default final-combiner c1
    0xB4 PSRGBOutputs[8]            rgb combiner outputs
    0xD4 PSCombinerCount            stages (1..8) | MUX_MSB<<8 | UNIQUE_C0<<12 | UNIQUE_C1<<16
    0xD8 PSTextureModes             5 bits/stage: how t0..t3 are addressed
    0xDC PSDotMapping               3 bits/stage for the DOT_* modes (stages 1..3)
    0xE0 PSInputTexture             which earlier stage feeds dependent lookups (stages 2,3)
    0xE4 PSC0Mapping                D3D constant index feeding each stage's c0 (4 bits/stage)
    0xE8 PSC1Mapping                same for c1
    0xEC PSFinalCombinerConstants   c0 | c1<<4 | flags<<8 for the final combiner

Each combiner input byte = register (bits 0-3) | channel (bit 4) | mapping (bits 5-7).

Usage:
    dx8_pixelshader_parse.py GAME.xbe --scan [--manifest game.json]
    dx8_pixelshader_parse.py GAME.xbe --def va:0x1E9B40 --manifest game.json [--json]
    dx8_pixelshader_parse.py GAME.xbe --scan --json > pixelshaders.json   (all decoded)
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass, field, asdict
from typing import Dict, List, Optional

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dx8_shader_parse as s2  # noqa: E402  (AddressSpace / ShaderError)

PSDEF_SIZE = 0xF0
PSDEF_FMT = "<8I I I 8I 8I 8I 8I I I I 8I I I I I I I I"
assert struct.calcsize(PSDEF_FMT) == PSDEF_SIZE

# --- register file ----------------------------------------------------------------
PS_REG = {0x0: "zero", 0x1: "c0", 0x2: "c1", 0x3: "fog", 0x4: "v0", 0x5: "v1",
          0x8: "t0", 0x9: "t1", 0xA: "t2", 0xB: "t3", 0xC: "r0", 0xD: "r1",
          0xE: "v1r0_sum", 0xF: "ef_prod"}
PS_REG_VALID = set(PS_REG)
PS_OUT_VALID = {0x0, 0x4, 0x5, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD}      # writable (0 = discard)

PS_MAPPING = {0x00: "unsigned_identity", 0x20: "unsigned_invert", 0x40: "expand_normal",
              0x60: "expand_negate", 0x80: "halfbias_normal", 0xA0: "halfbias_negate",
              0xC0: "signed_identity", 0xE0: "signed_negate"}

PS_TEXMODE = {0x00: "NONE", 0x01: "PROJECT2D", 0x02: "PROJECT3D", 0x03: "CUBEMAP", 0x04: "PASSTHRU",
              0x05: "CLIPPLANE", 0x06: "BUMPENVMAP", 0x07: "BUMPENVMAP_LUM", 0x08: "BRDF",
              0x09: "DOT_ST", 0x0A: "DOT_ZW", 0x0B: "DOT_RFLCT_DIFF", 0x0C: "DOT_RFLCT_SPEC",
              0x0D: "DOT_STR_3D", 0x0E: "DOT_STR_CUBE", 0x0F: "DPNDNT_AR", 0x10: "DPNDNT_GB",
              0x11: "DOTPRODUCT", 0x12: "DOT_RFLCT_SPEC_CONST"}

# output flags (bits 12-19 of an outputs dword)
OUT_SCALE = {0x00: "identity", 0x08: "bias", 0x10: "x2", 0x18: "x2_bias", 0x20: "x4", 0x30: "div2"}
OUT_AB_CD_MUX = 0x04
OUT_AB_DOT = 0x02
OUT_CD_DOT = 0x01
OUT_CD_BLUE_TO_ALPHA = 0x40
OUT_AB_BLUE_TO_ALPHA = 0x80

FINAL_CLAMP_SUM = 0x80
FINAL_COMPLEMENT_V1 = 0x40
FINAL_COMPLEMENT_R0 = 0x20


# --- model ------------------------------------------------------------------------

@dataclass
class PsInput:
    reg: str
    reg_code: int
    channel: str          # "rgb" | "blue" (rgb combiner)  /  "alpha" | "blue" (alpha combiner)
    mapping: str
    text: str


@dataclass
class PsOutputs:
    ab: str
    cd: str
    sum: str
    scale: str
    mux: bool             # sum slot is MUX(ab, cd) instead of ab + cd
    ab_dot: bool
    cd_dot: bool
    ab_blue_to_alpha: bool
    cd_blue_to_alpha: bool
    raw: int


@dataclass
class PsStage:
    index: int
    rgb_in: List[PsInput]
    rgb_out: PsOutputs
    alpha_in: List[PsInput]
    alpha_out: PsOutputs
    c0_default: str
    c1_default: str
    c0_map: int           # D3D pixel-shader constant index feeding c0
    c1_map: int
    text: List[str]


@dataclass
class PsTexStage:
    index: int
    mode: str
    mode_code: int
    compare_mode: int
    dot_mapping: int
    input_stage: Optional[int]


@dataclass
class PsFinal:
    present: bool
    a: Optional[PsInput]
    b: Optional[PsInput]
    c: Optional[PsInput]
    d: Optional[PsInput]
    e: Optional[PsInput]
    f: Optional[PsInput]
    g: Optional[PsInput]
    clamp_sum: bool
    complement_v1: bool
    complement_r0: bool
    c0_default: str
    c1_default: str
    c0_map: int
    c1_map: int
    flags: int
    text: List[str]


@dataclass
class PixelShaderDef:
    file_offset: int
    va: Optional[int]
    stage_count: int
    mux_msb: bool
    same_c0: bool
    same_c1: bool
    textures: List[PsTexStage]
    stages: List[PsStage]
    final: PsFinal
    registers_read: List[str]
    textures_used: List[int]
    constants_used: List[int]
    warnings: List[str] = field(default_factory=list)


# --- decoding ---------------------------------------------------------------------

def _input(byte: int, alpha: bool) -> PsInput:
    reg = byte & 0xF
    chan_bit = bool(byte & 0x10)
    mapping = byte & 0xE0
    # NV2A component usage: rgb inputs take .rgb or alpha-replicate (.aaa);
    # alpha inputs take .a or blue (.b).  Bit 4 selects the second option.
    if alpha:
        channel = "alpha" if chan_bit else "blue"
    else:
        channel = "alpha_rep" if chan_bit else "rgb"
    name = PS_REG.get(reg, f"reg?{reg}")
    mname = PS_MAPPING[mapping]
    suffix = {"rgb": ".rgb", "alpha_rep": ".aaa", "blue": ".b", "alpha": ".a"}[channel]
    return PsInput(reg=name, reg_code=reg, channel=channel, mapping=mname,
                   text=f"{name}{suffix}" + ("" if mname == "unsigned_identity" else f"<{mname}>"))


def _outputs(dword: int) -> PsOutputs:
    flags = (dword >> 12) & 0xFF
    scale = OUT_SCALE.get(flags & 0x38, f"scale?{flags & 0x38:#x}")
    return PsOutputs(ab=PS_REG.get((dword >> 4) & 0xF, "?"), cd=PS_REG.get(dword & 0xF, "?"),
                     sum=PS_REG.get((dword >> 8) & 0xF, "?"), scale=scale,
                     mux=bool(flags & OUT_AB_CD_MUX), ab_dot=bool(flags & OUT_AB_DOT),
                     cd_dot=bool(flags & OUT_CD_DOT), ab_blue_to_alpha=bool(flags & OUT_AB_BLUE_TO_ALPHA),
                     cd_blue_to_alpha=bool(flags & OUT_CD_BLUE_TO_ALPHA), raw=dword)


def _argb(v: int) -> str:
    return f"({((v >> 16) & 0xFF) / 255:.3f}, {((v >> 8) & 0xFF) / 255:.3f}, {(v & 0xFF) / 255:.3f}, {((v >> 24) & 0xFF) / 255:.3f})"


def _stage_text(i: int, rin, rout, ain, aout) -> List[str]:
    def combo(ins, out, dot_ok):
        a, b, c, d = ins
        ab = f"dot({a.text}, {b.text})" if (dot_ok and out.ab_dot) else f"{a.text} * {b.text}"
        cd = f"dot({c.text}, {d.text})" if (dot_ok and out.cd_dot) else f"{c.text} * {d.text}"
        parts = []
        if out.ab != "zero":
            parts.append(f"{out.ab} = {ab}")
        if out.cd != "zero":
            parts.append(f"{out.cd} = {cd}")
        if out.sum != "zero":
            parts.append(f"{out.sum} = {'mux' if out.mux else 'sum'}({ab}, {cd})")
        if not parts:
            parts.append("(discard)")
        sfx = "" if out.scale == "identity" else f"  [{out.scale}]"
        b2a = []
        if out.ab_blue_to_alpha:
            b2a.append("ab.blue->alpha")
        if out.cd_blue_to_alpha:
            b2a.append("cd.blue->alpha")
        return "; ".join(parts) + sfx + (f"  {{{', '.join(b2a)}}}" if b2a else "")
    return [f"stage {i} rgb  : {combo(rin, rout, True)}",
            f"stage {i} alpha: {combo(ain, aout, False)}"]


def decode_psdef(data: bytes, off: int, va: Optional[int] = None) -> PixelShaderDef:
    if off + PSDEF_SIZE > len(data):
        raise s2.ShaderError(f"pixel shader def at 0x{off:X} runs past end of data")
    f = struct.unpack_from(PSDEF_FMT, data, off)
    alpha_in = f[0:8]
    final_abcd, final_efg = f[8], f[9]
    const0 = f[10:18]
    const1 = f[18:26]
    alpha_out = f[26:34]
    rgb_in = f[34:42]
    compare_mode, final_c0, final_c1 = f[42], f[43], f[44]
    rgb_out = f[45:53]
    combiner_count, tex_modes, dot_mapping, input_texture, c0_map, c1_map, final_consts = f[53:60]

    warnings: List[str] = []
    n = combiner_count & 0xFF
    if not 1 <= n <= 8:
        warnings.append(f"PSCombinerCount {n} out of range 1..8")
        n = max(1, min(8, n))

    textures = []
    for t in range(4):
        mode = (tex_modes >> (5 * t)) & 0x1F
        textures.append(PsTexStage(
            index=t, mode=PS_TEXMODE.get(mode, f"?{mode}"), mode_code=mode,
            compare_mode=(compare_mode >> (4 * t)) & 0xF,
            dot_mapping=((dot_mapping >> (4 * (t - 1))) & 0xF) if t >= 1 else 0,
            input_stage=((input_texture >> (16 + 4 * (t - 2))) & 0x3) if t >= 2 else None))
        if mode not in PS_TEXMODE:
            warnings.append(f"texture stage {t}: unknown mode {mode}")

    stages = []
    regs_read, consts = set(), set()
    for i in range(n):
        rin = [_input((rgb_in[i] >> s) & 0xFF, False) for s in (24, 16, 8, 0)]
        ain = [_input((alpha_in[i] >> s) & 0xFF, True) for s in (24, 16, 8, 0)]
        rout, aout = _outputs(rgb_out[i]), _outputs(alpha_out[i])
        c0m = (c0_map >> (4 * i)) & 0xF
        c1m = (c1_map >> (4 * i)) & 0xF
        for inp in rin + ain:
            regs_read.add(inp.reg)
            if inp.reg == "c0":
                consts.add(c0m)
            if inp.reg == "c1":
                consts.add(c1m)
            if inp.reg_code not in PS_REG_VALID:
                warnings.append(f"stage {i}: invalid input register {inp.reg_code}")
        stages.append(PsStage(index=i, rgb_in=rin, rgb_out=rout, alpha_in=ain, alpha_out=aout,
                              c0_default=_argb(const0[i]), c1_default=_argb(const1[i]),
                              c0_map=c0m, c1_map=c1m, text=_stage_text(i, rin, rout, ain, aout)))

    fin_present = bool(final_abcd or final_efg)
    fa = fb = fc = fd = fe = ff = fg = None
    ftext: List[str] = []
    settings = final_efg & 0xFF
    if fin_present:
        fa, fb, fc, fd = [_input((final_abcd >> s) & 0xFF, False) for s in (24, 16, 8, 0)]
        fe, ff = [_input((final_efg >> s) & 0xFF, False) for s in (24, 16)]
        fg = _input((final_efg >> 8) & 0xFF, True)
        for inp in (fa, fb, fc, fd, fe, ff, fg):
            regs_read.add(inp.reg)
            if inp.reg == "c0":
                consts.add(final_consts & 0xF)
            if inp.reg == "c1":
                consts.add((final_consts >> 4) & 0xF)
        ftext = [f"final rgb  : {fa.text} * {fb.text} + (1 - {fa.text}) * {fc.text} + {fd.text}",
                 f"final alpha: {fg.text}",
                 f"final ef_prod = {fe.text} * {ff.text}"
                 + (";  v1r0_sum clamped" if settings & FINAL_CLAMP_SUM else "")
                 + (";  ~v1" if settings & FINAL_COMPLEMENT_V1 else "")
                 + (";  ~r0" if settings & FINAL_COMPLEMENT_R0 else "")]
    else:
        ftext = ["final      : (default) out = lerp(fog.rgb, r0.rgb, fog.a), r0.a"]

    final = PsFinal(present=fin_present, a=fa, b=fb, c=fc, d=fd, e=fe, f=ff, g=fg,
                    clamp_sum=bool(settings & FINAL_CLAMP_SUM),
                    complement_v1=bool(settings & FINAL_COMPLEMENT_V1),
                    complement_r0=bool(settings & FINAL_COMPLEMENT_R0),
                    c0_default=_argb(final_c0), c1_default=_argb(final_c1),
                    c0_map=final_consts & 0xF, c1_map=(final_consts >> 4) & 0xF,
                    flags=(final_consts >> 8) & 0xF, text=ftext)

    tex_used = sorted({int(r[1]) for r in regs_read if r in ("t0", "t1", "t2", "t3")})
    for t in tex_used:
        if textures[t].mode == "NONE":
            warnings.append(f"t{t} is read but texture stage {t} mode is NONE")

    return PixelShaderDef(file_offset=off, va=va, stage_count=n,
                          mux_msb=bool(combiner_count & 0x100), same_c0=not (combiner_count & 0x1000),   # bit 12 = UNIQUE_C0
                          same_c1=not (combiner_count & 0x10000),   # bit 16 = UNIQUE_C1
                          textures=textures, stages=stages,
                          final=final, registers_read=sorted(regs_read), textures_used=tex_used,
                          constants_used=sorted(consts), warnings=warnings)


# --- scanning ---------------------------------------------------------------------

def looks_like_psdef(data: bytes, off: int) -> bool:
    if off + PSDEF_SIZE > len(data):
        return False
    f = struct.unpack_from(PSDEF_FMT, data, off)
    combiner_count, tex_modes, dot_mapping, input_texture, c0_map, c1_map, final_consts = f[53:60]
    n = combiner_count & 0xFF
    if not 1 <= n <= 8 or combiner_count & ~0x111FF:
        return False
    if input_texture & ~0x330000 or f[42] & ~0xFFFF or dot_mapping & ~0xFFF or final_consts & ~0xFFF:
        return False
    for t in range(4):
        if ((tex_modes >> (5 * t)) & 0x1F) > 0x12:
            return False
    if tex_modes >> 20:
        return False
    alpha_in, alpha_out, rgb_in, rgb_out = f[0:8], f[26:34], f[34:42], f[45:53]
    any_nonzero = False
    for i in range(8):
        for dw in (rgb_in[i], alpha_in[i]):
            for s in (0, 8, 16, 24):
                if ((dw >> s) & 0xF) not in PS_REG_VALID:
                    return False
        for dw in (rgb_out[i], alpha_out[i]):
            if dw >> 20:
                return False
            if (dw & 0xF) not in PS_OUT_VALID or ((dw >> 4) & 0xF) not in PS_OUT_VALID \
                    or ((dw >> 8) & 0xF) not in PS_OUT_VALID:
                return False
        if i < n and (rgb_in[i] or alpha_in[i]):
            any_nonzero = True
        if i >= n and (rgb_in[i] or rgb_out[i] or alpha_in[i] or alpha_out[i]):
            return False                    # inactive stages are zero in assembler output
    if not any_nonzero:
        return False
    # stage 0 of a real shader always writes something
    if not (rgb_out[0] & 0xFFF) and not (alpha_out[0] & 0xFFF):
        return False
    return True


def scan_psdefs(data: bytes, aspace: s2.AddressSpace) -> List[int]:
    found = []
    p = 0
    while p + PSDEF_SIZE <= len(data):
        if looks_like_psdef(data, p):
            found.append(p)
            p += PSDEF_SIZE
        else:
            p += 4
    return found


# --- reporting --------------------------------------------------------------------

def print_psdef(d: PixelShaderDef, out=sys.stdout) -> None:
    p = lambda *a: print(*a, file=out)  # noqa: E731
    loc = f"file 0x{d.file_offset:X}" + (f" / VA 0x{d.va:08X}" if d.va is not None else "")
    p("=" * 78)
    p(f"Pixel Shader Def @ {loc}: {d.stage_count} combiner stage(s)"
      f"{', MUX_MSB' if d.mux_msb else ''}{', SAME_C0' if d.same_c0 else ''}{', SAME_C1' if d.same_c1 else ''}")
    p("=" * 78)
    for t in d.textures:
        if t.mode == "NONE" and t.index not in d.textures_used:
            continue
        extra = ""
        if t.mode == "CLIPPLANE":
            extra = f"  compare 0x{t.compare_mode:X}"
        if t.input_stage is not None and t.mode in ("DPNDNT_AR", "DPNDNT_GB", "BUMPENVMAP", "BUMPENVMAP_LUM",
                                                    "DOT_ST", "DOT_ZW", "DOT_RFLCT_DIFF", "DOT_RFLCT_SPEC",
                                                    "DOT_STR_3D", "DOT_STR_CUBE", "DOT_RFLCT_SPEC_CONST"):
            extra += f"  input t{t.input_stage}"
        p(f"  tex {t.index}: {t.mode}{extra}")
    for s in d.stages:
        for line in s.text:
            p(f"  {line}")
        cm = []
        if "c0" in (i.reg for i in s.rgb_in + s.alpha_in):
            cm.append(f"c0 = psc[{s.c0_map}] (default {s.c0_default})")
        if "c1" in (i.reg for i in s.rgb_in + s.alpha_in):
            cm.append(f"c1 = psc[{s.c1_map}] (default {s.c1_default})")
        if cm:
            p(f"           {'; '.join(cm)}")
    for line in d.final.text:
        p(f"  {line}")
    if d.final.present:
        cm = []
        if any(i and i.reg == "c0" for i in (d.final.a, d.final.b, d.final.c, d.final.d, d.final.e, d.final.f, d.final.g)):
            cm.append(f"c0 = psc[{d.final.c0_map}] (default {d.final.c0_default})")
        if any(i and i.reg == "c1" for i in (d.final.a, d.final.b, d.final.c, d.final.d, d.final.e, d.final.f, d.final.g)):
            cm.append(f"c1 = psc[{d.final.c1_map}] (default {d.final.c1_default})")
        if cm:
            p(f"           {'; '.join(cm)}")
    p(f"  reads: {' '.join(d.registers_read)}   textures: {' '.join(f't{t}' for t in d.textures_used) or '-'}"
      f"   constants: {' '.join(f'psc[{c}]' for c in d.constants_used) or '-'}")
    for w in d.warnings:
        p(f"  ! {w}")
    p("")


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(prog="dx8_pixelshader_parse.py",
                                 description="Decode Xbox D3DPIXELSHADERDEF register-combiner shaders (Xita Stage 2c).")
    ap.add_argument("file")
    ap.add_argument("--def", dest="psdef", metavar="LOC", help="location of one def (0x.. or va:0x..)")
    ap.add_argument("--scan", action="store_true", help="find all plausible defs in FILE")
    ap.add_argument("--manifest", help="Stage 1 JSON for VA translation")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)

    with open(args.file, "rb") as f:
        data = f.read()
    aspace = s2.AddressSpace.from_file(args.file, args.manifest)

    defs: List[PixelShaderDef] = []
    try:
        if args.psdef:
            off = aspace.resolve(args.psdef)
            defs.append(decode_psdef(data, off, aspace.offset_to_va(off)))
        elif args.scan:
            for off in scan_psdefs(data, aspace):
                defs.append(decode_psdef(data, off, aspace.offset_to_va(off)))
        else:
            ap.error("give --def LOC or --scan")
    except s2.ShaderError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps({"pixel_shaders": [asdict(d) for d in defs]}, indent=1))
        return 0
    if args.scan:
        print(f"Scan of {args.file}: {len(defs)} pixel shader def(s)")
    for d in defs:
        print_psdef(d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
