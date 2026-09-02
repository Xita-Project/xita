#!/usr/bin/env python3
"""
shader_recomp_gen.py - Stage 3 of the XboxVita offline static-recompilation pipeline.

Consumes the JSON emitted by `dx8_shader_parse.py --json` and writes a compilable
PlayStation Vita Cg vertex shader (.cg) for psp2cgc / GXP.

Generated layers
----------------
  1. struct AppIn   - one member per active v-register from the declaration's
                      `attributes` table, typed from the D3DVSDT enum and tagged
                      with a Vita semantic.
  2. struct VertOut - `float4 position : POSITION` (mandatory) plus only the
                      colour / texcoord / fog / point-size outputs the shader
                      actually produces.
  3. main()         - either
        * fixed-function: transform by the WVP rows held in the universal
          `uniform float4 c[]` array and pass colours / texcoords through, or
        * function:       a 1:1 translation of the decoded NV2A microcode
          (MAC + ILU, masks, swizzles, a0.x-relative constants) onto Cg.
     Mode is chosen automatically: if the JSON contains a decoded `function`
     block it is translated, otherwise fixed-function is emitted.

Uniform contract (documented at the top of every generated file)
----------------------------------------------------------------
  uniform float4 c[N]   indexed by (D3D constant index - C_BASE).
     fixed-function:  c[0..3] = rows of transpose(World*View*Projection)
                      c[4]    = viewport (2/w, -2/h, xoff, yoff)   [--rhw only]
                      c[5..8] = light dir, light colour, ambient, material diffuse [--light only]
     function:        exactly the range the microcode touches (or 96 entries
                      when a0.x-relative addressing is present).
  The runtime uploads SetVertexShaderConstant(reg, v) at c[reg - C_BASE].

Usage
-----
    shader_recomp_gen.py shader.json -o shader.cg
    shader_recomp_gen.py shader.json --mode ff --light -o ff_lit.cg
    shader_recomp_gen.py shader.json --binding-json shader.bind.json
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple

# ============================================================================
# Tables
# ============================================================================

# v-register -> (AppIn member name, Vita input semantic)
# GXM binds vertex attributes by parameter *name* (sceGxmProgramFindParameterByName)
# and resource index; the semantic only has to be a legal, unique one.
VREG_INPUT = {
    0:  ("position",   "POSITION"),
    1:  ("blendweight", "BLENDWEIGHT"),
    2:  ("normal",     "NORMAL"),
    3:  ("color0",     "COLOR0"),
    4:  ("color1",     "COLOR1"),
    5:  ("fog",        "TEXCOORD4"),
    6:  ("psize",      "TEXCOORD5"),
    7:  ("backcolor0", "TEXCOORD6"),
    8:  ("backcolor1", "TEXCOORD7"),
    9:  ("texcoord0",  "TEXCOORD0"),
    10: ("texcoord1",  "TEXCOORD1"),
    11: ("texcoord2",  "TEXCOORD2"),
    12: ("texcoord3",  "TEXCOORD3"),
    13: ("attr13",     "TEXCOORD8"),
    14: ("attr14",     "TEXCOORD9"),
    15: ("attr15",     "BLENDINDICES"),
}

# D3DVSDT name -> (Cg declared type, component count, expansion into float4, note)
# `expand` is a format string with {v} = the AppIn member expression.
VSDT_CG = {
    "FLOAT1":      ("float",  1, "float4({v}, 0.0, 0.0, 1.0)", ""),
    "FLOAT2":      ("float2", 2, "float4({v}, 0.0, 1.0)",      ""),
    "FLOAT3":      ("float3", 3, "float4({v}, 1.0)",           ""),
    "FLOAT4":      ("float4", 4, "{v}",                        ""),
    "D3DCOLOR":    ("float4", 4, "{v}.zyxw",                   "U8N x4 in memory order B,G,R,A -> swizzled to RGBA"),
    "SHORT2":      ("float2", 2, "float4({v}, 0.0, 1.0)",      "S16 (not normalised)"),
    "SHORT4":      ("float4", 4, "{v}",                        "S16 (not normalised)"),
    "NORMSHORT1":  ("float",  1, "float4({v}, 0.0, 0.0, 1.0)", "S16N"),
    "NORMSHORT2":  ("float2", 2, "float4({v}, 0.0, 1.0)",      "S16N"),
    "NORMSHORT3":  ("float3", 3, "float4({v}, 1.0)",           "S16N"),
    "NORMSHORT4":  ("float4", 4, "{v}",                        "S16N"),
    "NORMPACKED3": ("float4", 4, "float4(xv_unpack_normpacked3({v}), 1.0)", "bind as U8 x4 (raw bytes); unpacked in-shader"),
    "FLOAT2H":     ("float3", 3, "float4({v}.x, {v}.y, 0.0, {v}.z)", "F32 x3 = (x, y, w)"),
    "PBYTE1":      ("float",  1, "float4({v}, 0.0, 0.0, 1.0)", "U8N"),
    "PBYTE2":      ("float2", 2, "float4({v}, 0.0, 1.0)",      "U8N"),
    "PBYTE3":      ("float3", 3, "float4({v}, 1.0)",           "U8N"),
    "PBYTE4":      ("float4", 4, "{v}",                        "U8N"),
}

# NV2A output register -> (VertOut member, Cg type, Vita semantic)
OREG_OUTPUT = {
    "oPos": ("position",  "float4", "POSITION"),
    "oD0":  ("color0",    "float4", "COLOR0"),
    "oD1":  ("color1",    "float4", "COLOR1"),
    "oFog": ("fog",       "float",  "TEXCOORD7"),   # fragment side reads the fog factor here
    "oPts": ("psize",     "float",  "PSIZE"),
    "oB0":  ("backcolor0", "float4", "TEXCOORD8"),
    "oB1":  ("backcolor1", "float4", "TEXCOORD9"),
    "oT0":  ("texcoord0", "float4", "TEXCOORD0"),
    "oT1":  ("texcoord1", "float4", "TEXCOORD1"),
    "oT2":  ("texcoord2", "float4", "TEXCOORD2"),
    "oT3":  ("texcoord3", "float4", "TEXCOORD3"),
}
OREG_ORDER = list(OREG_OUTPUT)

SWZ = "xyzw"

# ILU / MAC helper bodies, emitted only when used.
HELPERS = {
    "xv_unpack_normpacked3": """\
// D3DVSDT_NORMPACKED3: 11:11:10 signed-normalised packed into one DWORD.
// Attribute is bound as U8 x4 (raw, not normalised) so b = bytes 0..3 as floats.
float3 xv_unpack_normpacked3(float4 b)
{
    float x = b.x + fmod(b.y, 8.0) * 256.0;                    // bits  0..10
    float y = floor(b.y / 8.0) + fmod(b.z, 64.0) * 32.0;       // bits 11..21
    float z = floor(b.z / 64.0) + b.w * 4.0;                   // bits 22..31
    x = (x >= 1024.0) ? x - 2048.0 : x;
    y = (y >= 1024.0) ? y - 2048.0 : y;
    z = (z >=  512.0) ? z - 1024.0 : z;
    return float3(x / 1023.0, y / 1023.0, z / 511.0);
}""",
    "xv_rcp": """\
float4 xv_rcp(float4 s) { return float4(1.0 / s.x); }""",
    "xv_rcc": """\
// Reciprocal with NV2A range clamp (sign preserved).
float4 xv_rcc(float4 s)
{
    float r = 1.0 / s.x;
    r = (r > 0.0) ? clamp(r, 5.42101e-20, 1.884467e19) : clamp(r, -1.884467e19, -5.42101e-20);
    return float4(r);
}""",
    "xv_rsq": """\
float4 xv_rsq(float4 s) { return float4(rsqrt(abs(s.x))); }""",
    "xv_exp": """\
float4 xv_exp(float4 s) { return float4(exp2(floor(s.x)), frac(s.x), exp2(s.x), 1.0); }""",
    "xv_log": """\
float4 xv_log(float4 s)
{
    float a = abs(s.x);
    float e = floor(log2(a));
    return float4(e, a / exp2(e), log2(a), 1.0);
}""",
    "xv_lit": """\
// NV2A LIT: (1, max(N.L,0), (N.L>0) ? max(N.H,0)^p : 0, 1), p clamped to +-128.
float4 xv_lit(float4 s)
{
    float p = clamp(s.w, -128.0, 128.0);
    float d = max(s.x, 0.0);
    float sp = (s.x > 0.0) ? pow(max(s.y, 0.0), p) : 0.0;
    return float4(1.0, d, sp, 1.0);
}""",
    "xv_dst": """\
float4 xv_dst(float4 a, float4 b) { return float4(1.0, a.y * b.y, a.z, b.w); }""",
    "xv_dph": """\
float4 xv_dph(float4 a, float4 b) { return float4(dot(a.xyz, b.xyz) + b.w); }""",
}

ILU_HELPER = {"rcp": "xv_rcp", "rcc": "xv_rcc", "rsq": "xv_rsq", "exp": "xv_exp",
              "log": "xv_log", "lit": "xv_lit"}


class GenError(Exception):
    pass


# ============================================================================
# Model
# ============================================================================

@dataclass
class InputAttr:
    vreg: int
    name: str
    semantic: str
    cg_type: str
    dtype: str
    stream: Optional[int]
    offset: Optional[int]
    size: Optional[int]
    expand: str           # expression producing a float4 from IN.<name>
    note: str


@dataclass
class OutputVar:
    oreg: str
    name: str
    cg_type: str
    semantic: str


@dataclass
class Plan:
    mode: str
    inputs: List[InputAttr]
    outputs: List[OutputVar]
    c_base: int
    c_count: int
    const_bias: int
    helpers: Set[str] = field(default_factory=set)
    warnings: List[str] = field(default_factory=list)
    source_desc: str = ""
    skip_slots: Set[int] = field(default_factory=set)   # microcode slots not translated (viewport epilogue)


# ============================================================================
# Planning
# ============================================================================

def load_stage2(path: str) -> Tuple[Optional[dict], Optional[dict]]:
    with open(path, "r", encoding="utf-8") as f:
        doc = json.load(f)
    decl = doc.get("declaration")
    func = doc.get("function")
    if decl is None and func is None:
        raise GenError(f"{path}: JSON has neither a 'declaration' nor a 'function' block")
    return decl, func


def plan_inputs(decl: Optional[dict], func: Optional[dict], warnings: List[str]) -> List[InputAttr]:
    inputs: Dict[int, InputAttr] = {}
    if decl:
        for a in decl.get("attributes", []):
            vreg = int(a["vreg"])
            dtype = a["type"]
            if dtype == "NONE":
                continue
            if vreg not in VREG_INPUT:
                warnings.append(f"v{vreg} has no input mapping; skipped")
                continue
            if dtype in VSDT_CG:
                cg_type, _n, expand, note = VSDT_CG[dtype]
            else:
                # Generic NV2A code from Stage 2 (e.g. "S32Kx1", "UB_OGLx3"): width from the suffix.
                n = int(dtype[-1]) if dtype[-2:-1] == "x" and dtype[-1].isdigit() else 4
                cg_type = "float" if n == 1 else f"float{n}"
                expand = {1: "float4({v}, 0.0, 0.0, 1.0)", 2: "float4({v}, 0.0, 1.0)",
                          3: "float4({v}, 1.0)", 4: "{v}"}[n]
                note = f"generic {dtype}"
                if not (dtype[-2:-1] == "x" and dtype[-1].isdigit()):
                    warnings.append(f"v{vreg}: unknown data type {dtype}; declared as float4 passthrough")
            name, sem = VREG_INPUT[vreg]
            if vreg in inputs:
                warnings.append(f"v{vreg} declared twice; keeping first")
                continue
            inputs[vreg] = InputAttr(vreg=vreg, name=name, semantic=sem, cg_type=cg_type, dtype=dtype,
                                     stream=a.get("stream"), offset=a.get("offset"), size=a.get("size"),
                                     expand=expand, note=note)
    if func:
        for vname in func.get("inputs_used", []):
            vreg = int(vname[1:])
            if vreg not in inputs:
                name, sem = VREG_INPUT[vreg]
                warnings.append(f"microcode reads {vname} but the declaration does not supply it; "
                                f"declared as float4 (bind a constant stream or default (0,0,0,1))")
                inputs[vreg] = InputAttr(vreg=vreg, name=name, semantic=sem, cg_type="float4", dtype="FLOAT4",
                                         stream=None, offset=None, size=None, expand="{v}",
                                         note="not in declaration")
    return [inputs[k] for k in sorted(inputs)]


def plan_outputs(oregs: List[str]) -> List[OutputVar]:
    names = set(oregs) | {"oPos"}
    return [OutputVar(oreg=o, name=OREG_OUTPUT[o][0], cg_type=OREG_OUTPUT[o][1], semantic=OREG_OUTPUT[o][2])
            for o in OREG_ORDER if o in names]


def plan_ff(decl: Optional[dict], func: Optional[dict], args, src: str) -> Plan:
    warnings: List[str] = []
    inputs = plan_inputs(decl, None, warnings)
    have = {i.vreg for i in inputs}
    if 0 not in have:
        raise GenError("fixed-function mode needs a v0 (POSITION) attribute in the declaration")
    oregs = ["oPos"]
    if 3 in have or args.light:
        oregs.append("oD0")
    if 4 in have:
        oregs.append("oD1")
    for t in range(4):
        if 9 + t in have:
            oregs.append(f"oT{t}")
    c_count = 4
    if args.rhw:
        c_count = 5
    if args.light:
        c_count = 9
        if 2 not in have:
            warnings.append("--light requested but no v2 NORMAL attribute; lighting reduced to ambient+material")
    if args.const_count:
        c_count = max(c_count, args.const_count)
    p = Plan(mode="ff", inputs=inputs, outputs=plan_outputs(oregs), c_base=0, c_count=c_count,
             const_bias=96, warnings=warnings, source_desc=src)
    for i in inputs:
        if i.dtype == "NORMPACKED3":
            p.helpers.add("xv_unpack_normpacked3")
    return p


def detect_viewport_epilogue(func: dict) -> Set[int]:
    """
    Xbox D3D appends a screen-space transform to every vertex program:
        mul oPos.xyz, r12, c[58]      (c-38: viewport scale)   + rcc r1.x, r12.w
        mad oPos.xyz, r12, r1.x, c[59](c-37: viewport offset)
    GXM consumes clip space and divides by w itself, so these slots must go.
    Returns the set of slot indices forming that tail (empty if absent).
    The XDK scheduler interleaves these with unrelated work (Halo: the mad can sit
    eight slots after the mul/rcc pair), so the pair is searched for anywhere.
    """
    ins = func.get("instructions", [])

    def ops_of(i, unit):
        return [op for op in i["ops"] if op["unit"] == unit]

    def writes_opos_xyz(op):
        return any(o["kind"] == "o" and o["name"] == "oPos" and int(o["mask"]) == 0xE for o in op["outputs"])

    def reads(op, kind, index):
        return any(i["kind"] == kind and int(i["index"]) == index for i in op["inputs"])

    def writes_r1x(op):
        return any(o["kind"] == "r" and int(o["index"]) == 1 and int(o["mask"]) & 0x8 for o in op["outputs"])

    mul_slot = None
    for i in ins:
        mul = [op for op in ops_of(i, "MAC") if op["opcode"] == "mul"]
        rcc = [op for op in ops_of(i, "ILU") if op["opcode"] == "rcc"]
        if (mul and rcc and writes_opos_xyz(mul[0]) and reads(mul[0], "r", 12)
                and reads(mul[0], "c", 58) and reads(rcc[0], "r", 12) and writes_r1x(rcc[0])):
            mul_slot = i["slot"]
            break
    if mul_slot is None:
        return set()
    for i in ins:
        if i["slot"] <= mul_slot:
            continue
        mad = [op for op in ops_of(i, "MAC") if op["opcode"] == "mad"]
        if (mad and writes_opos_xyz(mad[0]) and reads(mad[0], "r", 12) and reads(mad[0], "r", 1)
                and reads(mad[0], "c", 59)):
            return {mul_slot, i["slot"]}
        # r1.x must survive untouched between the two halves for this to be the XDK epilogue
        if any(writes_r1x(op) for op in i["ops"]):
            return set()
    return set()


def plan_function(decl: Optional[dict], func: dict, args, src: str) -> Plan:
    warnings: List[str] = []
    inputs = plan_inputs(decl, func, warnings)
    bias = int(func.get("const_bias", 96))
    skip: Set[int] = set() if args.keep_viewport_epilogue else detect_viewport_epilogue(func)
    if skip:
        warnings.append(f"stripped Xbox viewport epilogue (slots {sorted(skip)}): GXM performs the "
                        f"perspective divide; c[58]/c[59] are no longer referenced")
    live_ops = [op for ins in func["instructions"] if ins["slot"] not in skip for op in ins["ops"]]
    hw = sorted({int(i["index"]) for op in live_ops for i in op["inputs"] if i["kind"] == "c"})
    d3d = [c - bias for c in hw]
    relative = any(i.get("relative") for op in live_ops for i in op["inputs"])
    if d3d:
        lo, hi = min(d3d), max(d3d)
    else:
        lo, hi = 0, -1
    if relative:
        lo, hi = min(lo, 0), max(hi, 95)
        warnings.append("a0.x-relative constant addressing present: full 96-entry array emitted; "
                        "dynamic uniform indexing costs extra USSE cycles")
    c_base = lo if d3d or relative else 0
    c_count = (hi - c_base + 1) if hi >= c_base else 1
    if args.const_count:
        if args.const_count < c_count:
            warnings.append(f"--const-count {args.const_count} smaller than required {c_count}; ignored")
        else:
            c_count = args.const_count
    p = Plan(mode="function", inputs=inputs, outputs=plan_outputs(func.get("outputs_used", [])),
             c_base=c_base, c_count=c_count, const_bias=bias, warnings=warnings, source_desc=src,
             skip_slots=skip)
    for i in inputs:
        if i.dtype == "NORMPACKED3":
            p.helpers.add("xv_unpack_normpacked3")
    for op in live_ops:
        if op["unit"] == "ILU" and op["opcode"] in ILU_HELPER:
            p.helpers.add(ILU_HELPER[op["opcode"]])
        elif op["opcode"] == "dst":
            p.helpers.add("xv_dst")
        elif op["opcode"] == "dph":
            p.helpers.add("xv_dph")
    return p


# ============================================================================
# Emission - shared blocks
# ============================================================================

def emit_header(p: Plan, args, extra: List[str]) -> List[str]:
    L = [
        "// ---------------------------------------------------------------------------",
        "//  XboxVita Stage 3 - auto-generated Vita Cg vertex shader",
        f"//  source : {p.source_desc}",
        f"//  mode   : {'fixed-function' if p.mode == 'ff' else 'NV2A microcode translation'}",
        "//  target : psp2cgc -profile sce_vp_psp2",
        "//",
        "//  Uniform contract:",
        f"//    uniform float4 c[{p.c_count}]  - universal constant array, C_BASE = {p.c_base}",
        f"//    runtime maps SetVertexShaderConstant(reg, v) -> c[reg - C_BASE]",
    ]
    L += [f"//    {e}" for e in extra]
    L += [
        "//  Attribute binding (sceGxmVertexAttribute regIndex from parameter name):",
    ]
    for i in p.inputs:
        loc = (f"stream {i.stream} +{i.offset} ({i.size} B)" if i.stream is not None else "not in declaration")
        L.append(f"//    v{i.vreg:<2} {i.name:<11} {i.cg_type:<6} {i.dtype:<11} {loc}"
                 + (f"  // {i.note}" if i.note and i.note != loc else ""))
    L += [
        "//  Depth: D3D clip z in [0, w] is passed through unchanged; set the GXM viewport",
        "//         with zOffset = 0, zScale = 1 (no [-1,1] -> [0,1] remap) to match.",
        "// ---------------------------------------------------------------------------",
        "",
    ]
    return L


def emit_appin(p: Plan) -> List[str]:
    L = ["struct AppIn", "{"]
    for i in p.inputs:
        L.append(f"    {i.cg_type:<6} {i.name:<11} : {i.semantic};" + (f"   // v{i.vreg} {i.dtype}"))
    L += ["};", ""]
    return L


def emit_vertout(p: Plan) -> List[str]:
    L = ["struct VertOut", "{"]
    for o in p.outputs:
        L.append(f"    {o.cg_type:<6} {o.name:<11} : {o.semantic};" + f"   // {o.oreg}")
    L += ["};", ""]
    return L


def emit_helpers(p: Plan) -> List[str]:
    L: List[str] = []
    for h in sorted(p.helpers):
        L += HELPERS[h].splitlines()
        L.append("")
    return L


def emit_input_loads(p: Plan, needed: Optional[Set[int]] = None) -> List[str]:
    """float4 vN = <expanded IN.member>; for every declared attribute."""
    L = ["    // --- attribute loads (expanded to float4 exactly as the NV2A input file presents them)"]
    for i in p.inputs:
        if needed is not None and i.vreg not in needed:
            continue
        expr = i.expand.format(v=f"IN.{i.name}")
        L.append(f"    float4 v{i.vreg} = {expr};" + (f"   // {i.note}" if i.note else ""))
    return L


def emit_output_assign(p: Plan, regs: Dict[str, str]) -> List[str]:
    """Copy accumulated o-register locals into OUT with correct component width."""
    L = ["", "    // --- outputs"]
    for o in p.outputs:
        src = regs.get(o.oreg)
        if src is None:
            src = "float4(0.0, 0.0, 0.0, 1.0)" if o.cg_type == "float4" else "0.0"
        elif o.cg_type == "float":
            src = f"{src}.x"
        L.append(f"    OUT.{o.name} = {src};")
    L += ["    return OUT;"]
    return L


# ============================================================================
# Emission - fixed-function main
# ============================================================================

def emit_main_ff(p: Plan, args) -> List[str]:
    have = {i.vreg: i for i in p.inputs}
    L = [
        f"VertOut main(AppIn IN, uniform float4 c[{p.c_count}])",
        "{",
        "    VertOut OUT;",
    ]
    L += emit_input_loads(p)
    L.append("")
    if args.rhw:
        L += [
            "    // --- pre-transformed (XYZRHW) path: undo the perspective divide so the",
            "    //     rasteriser redoes it.  c[4] = (2/width, -2/height, xoffset, yoffset).",
            "    float  w   = 1.0 / v0.w;",
            "    float2 ndc = v0.xy * c[4].xy + c[4].zw;",
            "    float4 oPos = float4(ndc * w, v0.z * w, w);",
        ]
    else:
        L += [
            "    // --- transform: c[0..3] hold the rows of transpose(World*View*Projection),",
            "    //     i.e. the classic vs.1.1 `dp4 oPos.x, v0, c0` prologue.",
            "    float4 oPos;",
            "    oPos.x = dot(v0, c[0]);",
            "    oPos.y = dot(v0, c[1]);",
            "    oPos.z = dot(v0, c[2]);",
            "    oPos.w = dot(v0, c[3]);",
        ]
    regs = {"oPos": "oPos"}

    if args.light:
        L += [
            "",
            "    // --- single directional light (c[5] = -light dir, object space; c[6] = light colour;",
            "    //     c[7] = ambient; c[8] = material diffuse)",
        ]
        base = "v3" if 3 in have else "c[8]"
        if 2 in have:
            L += [
                "    float  ndl  = max(dot(normalize(v2.xyz), c[5].xyz), 0.0);",
                f"    float4 oD0  = float4(saturate(c[7].rgb + c[6].rgb * ndl) * {base}.rgb, {base}.a);",
            ]
        else:
            L += [f"    float4 oD0  = float4(saturate(c[7].rgb) * {base}.rgb, {base}.a);"]
        regs["oD0"] = "oD0"
    elif 3 in have:
        regs["oD0"] = "v3"
    if 4 in have:
        regs["oD1"] = "v4"
    for t in range(4):
        if 9 + t in have:
            regs[f"oT{t}"] = f"v{9 + t}"
    L += emit_output_assign(p, regs)
    L += ["}", ""]
    return L


# ============================================================================
# Emission - microcode translation main
# ============================================================================

def _src_expr(operand: dict, p: Plan) -> str:
    kind = operand["kind"]
    if kind == "v":
        base = f"v{operand['index']}"
    elif kind == "r":
        base = "oPos" if operand["index"] == 12 else f"r{operand['index']}"
    elif kind == "c":
        d3d = int(operand["index"]) - p.const_bias - p.c_base
        base = f"c[a0 + {d3d}]" if operand.get("relative") else f"c[{d3d}]"
    else:
        base = "float4(0.0, 0.0, 0.0, 0.0)"
    swz = operand.get("swizzle", "xyzw")
    if swz != "xyzw":
        base = f"{base}.{swz}"
    return f"-{base}" if operand.get("negate") else base


def _mac_expr(op: dict, p: Plan) -> str:
    a = [_src_expr(i, p) for i in op["inputs"]]
    o = op["opcode"]
    if o == "mov":
        return a[0]
    if o == "mul":
        return f"{a[0]} * {a[1]}"
    if o == "add":
        return f"{a[0]} + {a[1]}"
    if o == "mad":
        return f"{a[0]} * {a[1]} + {a[2]}"
    if o == "dp3":
        return f"float4(dot(({a[0]}).xyz, ({a[1]}).xyz))"
    if o == "dph":
        return f"xv_dph({a[0]}, {a[1]})"
    if o == "dp4":
        return f"float4(dot({a[0]}, {a[1]}))"
    if o == "dst":
        return f"xv_dst({a[0]}, {a[1]})"
    if o == "min":
        return f"min({a[0]}, {a[1]})"
    if o == "max":
        return f"max({a[0]}, {a[1]})"
    if o == "slt":
        return f"(float4(1.0) - step({a[1]}, {a[0]}))"
    if o == "sge":
        return f"step({a[1]}, {a[0]})"
    raise GenError(f"unsupported MAC opcode {o}")


def _ilu_expr(op: dict, p: Plan) -> str:
    a = _src_expr(op["inputs"][0], p)
    o = op["opcode"]
    if o == "mov":
        return a
    return f"{ILU_HELPER[o]}({a})"


def _mask_suffix(mask: int) -> str:
    if mask == 0xF:
        return ""
    return "." + "".join(ch for bit, ch in ((8, "x"), (4, "y"), (2, "z"), (1, "w")) if mask & bit)


def emit_main_function(p: Plan, func: dict, args) -> List[str]:
    L = [
        f"VertOut main(AppIn IN, uniform float4 c[{p.c_count}])",
        "{",
        "    VertOut OUT;",
    ]
    # Load only the v-registers the translated code reads.  Declared-but-unused
    # attributes stay in AppIn (they define the stream layout) but get no local,
    # which keeps SceShaccCg's "unreferenced variable" warnings out of the log.
    needed = {int(o["index"]) for ins in func["instructions"] if ins["slot"] not in p.skip_slots
              for op in ins["ops"] for o in op["inputs"] if o["kind"] == "v"}
    L += emit_input_loads(p, needed)

    # Register file sized from the slots actually translated (stripped epilogue
    # slots excluded), so SceShaccCg does not warn about unreferenced temps.
    live = [op for ins in func["instructions"] if ins["slot"] not in p.skip_slots for op in ins["ops"]]
    temps = sorted({int(o["index"]) for op in live for o in op["inputs"] + op["outputs"]
                    if o["kind"] == "r" and int(o["index"]) != 12})
    L += ["", "    // --- register file"]
    if temps:
        L.append("    float4 " + ", ".join(f"r{t} = float4(0.0, 0.0, 0.0, 0.0)" for t in temps) + ";")
    oregs_written = [o.oreg for o in p.outputs]
    L.append("    float4 " + ", ".join(f"{o} = float4(0.0, 0.0, 0.0, {'1.0' if o == 'oPos' else '0.0'})"
                                       for o in oregs_written) + ";")
    uses_a0 = any(op["opcode"] == "arl" for ins in func["instructions"] for op in ins["ops"])
    if uses_a0:
        L.append("    int a0 = 0;")
    L.append("")
    L.append("    // --- program (one NV2A slot per block; MAC and ILU read before either writes)")

    for ins in func["instructions"]:
        slot = ins["slot"]
        ops = [op for op in ins["ops"] if op["opcode"] != "nop"]
        if not ops:
            L.append(f"    // [{slot:02d}] nop")
            continue
        listing = " | ".join(op["text"] for op in ops)
        if slot in p.skip_slots:
            L.append(f"    // [{slot:02d}] {listing}    <- stripped: Xbox viewport epilogue (GXM divides by w)")
            continue
        L.append(f"    // [{slot:02d}] {listing}")
        L.append("    {")
        # 1. evaluate
        for op in ops:
            if op["opcode"] == "arl":
                continue
            expr = _mac_expr(op, p) if op["unit"] == "MAC" else _ilu_expr(op, p)
            L.append(f"        float4 {'m' if op['unit'] == 'MAC' else 'i'} = {expr};")
        # 2. write back
        for op in ops:
            tmp = "m" if op["unit"] == "MAC" else "i"
            if op["opcode"] == "arl":
                L.append(f"        a0 = (int)floor(({_src_expr(op['inputs'][0], p)}).x);")
                continue
            for out in op["outputs"]:
                sfx = _mask_suffix(int(out["mask"]))
                if out["kind"] == "r":
                    L.append(f"        r{out['index']}{sfx} = {tmp}{sfx};")
                elif out["kind"] == "o":
                    L.append(f"        {out['name']}{sfx} = {tmp}{sfx};")
                elif out["kind"] == "c":
                    p.warnings.append(f"slot {slot}: write to constant c[{out['index']}] cannot be expressed "
                                      f"in a Cg vertex shader (state-shader semantics); dropped")
                    L.append(f"        // constant write c[{out['index']}]{sfx} = {tmp}{sfx};  (unsupported)")
            if not op["outputs"]:
                L.append(f"        // result of {op['opcode']} discarded")
        L.append("    }")

    regs = {o: o for o in oregs_written}
    L += emit_output_assign(p, regs)
    L += ["}", ""]
    return L


# ============================================================================
# Driver
# ============================================================================

def generate(decl: Optional[dict], func: Optional[dict], args, src: str) -> Tuple[str, Plan]:
    mode = args.mode
    if mode == "auto":
        mode = "function" if func else "ff"
    if mode == "function" and not func:
        raise GenError("--mode function requested but the JSON has no decoded 'function' block")

    if mode == "ff":
        p = plan_ff(decl, func, args, src)
        extra = ["c[0..3] = rows of transpose(World*View*Projection)"]
        if args.rhw:
            extra.append("c[4]    = viewport (2/width, -2/height, xoffset, yoffset)")
        if args.light:
            extra.append("c[5..8] = -light dir (object space), light colour, ambient, material diffuse")
        body = emit_main_ff(p, args)
    else:
        p = plan_function(decl, func, args, src)
        extra = [f"microcode uses D3D c{p.c_base}..c{p.c_base + p.c_count - 1} "
                 f"(hardware c[{p.c_base + p.const_bias}]..)",
                 f"{func['decoded_count'] - len(p.skip_slots)} of {func['decoded_count']} NV2A slots translated; "
                 f"inputs {' '.join(func.get('inputs_used', [])) or '-'}; "
                 f"outputs {' '.join(func.get('outputs_used', [])) or '-'}"]
        if p.skip_slots:
            extra.append(f"slots {sorted(p.skip_slots)} = Xbox viewport epilogue, stripped "
                         f"(pass --keep-viewport-epilogue to translate verbatim)")
        body = emit_main_function(p, func, args)

    lines = emit_header(p, args, extra) + emit_appin(p) + emit_vertout(p) + emit_helpers(p) + body
    return "\n".join(lines), p


def binding_manifest(p: Plan, out_path: str) -> dict:
    return {
        "cg_file": out_path,
        "mode": p.mode,
        "uniform": {"name": "c", "count": p.c_count, "c_base": p.c_base, "const_bias": p.const_bias},
        "attributes": [
            {"vreg": i.vreg, "parameter": i.name, "cg_type": i.cg_type, "d3d_type": i.dtype,
             "stream": i.stream, "offset": i.offset, "size": i.size}
            for i in p.inputs
        ],
        "outputs": [{"oreg": o.oreg, "member": o.name, "semantic": o.semantic} for o in p.outputs],
        "warnings": p.warnings,
    }


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        prog="shader_recomp_gen.py",
        description="Generate a Vita Cg vertex shader from dx8_shader_parse.py --json output (XboxVita Stage 3).")
    ap.add_argument("json", help="Stage 2 JSON (dx8_shader_parse.py ... --json > file)")
    ap.add_argument("-o", "--output", help="output .cg path (default: <json basename>.cg)")
    ap.add_argument("--mode", choices=["auto", "ff", "function"], default="auto",
                    help="auto: translate microcode if present, else fixed-function (default)")
    ap.add_argument("--rhw", action="store_true", help="fixed-function: treat v0 as pre-transformed XYZRHW")
    ap.add_argument("--light", action="store_true", help="fixed-function: add one directional light")
    ap.add_argument("--const-count", type=int, default=0,
                    help="force the size of the universal c[] array (e.g. 96 for a fixed runtime layout)")
    ap.add_argument("--keep-viewport-epilogue", action="store_true",
                    help="function mode: translate the Xbox screen-space epilogue instead of stripping it")
    ap.add_argument("--binding-json", metavar="PATH",
                    help="also write a runtime binding manifest (parameter names, streams, uniform layout)")
    ap.add_argument("--stdout", action="store_true", help="print the shader instead of writing a file")
    args = ap.parse_args(argv)

    try:
        decl, func = load_stage2(args.json)
        src = os.path.basename(args.json)
        text, plan = generate(decl, func, args, src)
    except (OSError, ValueError, KeyError) as e:
        print(f"error: cannot read {args.json}: {e}", file=sys.stderr)
        return 2
    except GenError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    out_path = args.output or (os.path.splitext(args.json)[0] + ".cg")
    if args.stdout:
        sys.stdout.write(text + "\n")
    else:
        with open(out_path, "w", encoding="utf-8") as f:
            f.write(text + "\n")
        print(f"wrote {out_path}  ({plan.mode}, {len(plan.inputs)} inputs, {len(plan.outputs)} outputs, "
              f"c[{plan.c_count}] base {plan.c_base})")
    if args.binding_json:
        with open(args.binding_json, "w", encoding="utf-8") as f:
            json.dump(binding_manifest(plan, out_path), f, indent=2)
        print(f"wrote {args.binding_json}")
    for w in plan.warnings:
        print(f"warning: {w}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
