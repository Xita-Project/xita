#!/usr/bin/env python3
"""
pixelshader_recomp_gen.py - Stage 3b of the XboxVita pipeline: NV2A register
combiners -> Vita Cg fragment shader.

Input: the JSON written by `dx8_pixelshader_parse.py --json` (one or more decoded
D3DPIXELSHADERDEF structs).  Output: one `<name>.frag.cg` per definition, which
tools/shadercomp compiles on the Vita as a fragment program.

NV2A combiner model reproduced here
-----------------------------------
  registers   : r0 r1 (signed [-1,1]), t0..t3 (texture results), v0 v1 (interpolated
                colours), c0 c1 (per-stage constants), fog (rgb colour, a = factor),
                zero, and in the final combiner ef_prod (E*F) and v1r0_sum (v1 + r0).
  per stage   : rgb: ab = A*B (or dot), cd = C*D (or dot), third = ab+cd or MUX(ab,cd)
                on r0.a; alpha: same with scalar inputs; each result scaled by the
                output flags (bias / x2 / x2_bias / x4 / div2) and clamped to [-1,1];
                blue-to-alpha routes ab/cd blue into the destination's alpha.
  input map   : unsigned_identity/invert, expand_normal/negate, halfbias_normal/negate,
                signed_identity/negate.
  final       : rgb = A*B + (1-A)*C + D, alpha = G ; default (no final combiner in the
                def) = lerp(fog.rgb, r0.rgb, fog.a), r0.a - which is what Xbox D3D
                installs for fixed-function fog.
  textures    : PROJECT2D/PROJECT3D/CUBEMAP/PASSTHRU/CLIPPLANE/DPNDNT_AR/DPNDNT_GB/
                BUMPENVMAP(_LUM) implemented; the DOT_*/BRDF modes fall back to a plain
                2D fetch with a warning (they need the whole dot-product pipeline).

Uniform contract of every generated fragment shader
---------------------------------------------------
  uniform sampler2D/samplerCUBE tex0..tex3   (only the stages the shader samples)
  uniform float4 psc[16]      D3D pixel-shader constants (SetPixelShaderConstant);
                              stage i reads c0 = psc[c0_map[i]], c1 = psc[c1_map[i]]
  uniform float4 xv_fogcolor  fog colour (rgb); the fog FACTOR arrives per vertex
                              in the TEXCOORD7 varying written by the vertex shader
  uniform float4 xv_bumpmat[4], xv_bumplum[4]   D3DTSS_BUMPENVMAT00..11 / LSCALE,LOFFSET

Usage:
    pixelshader_recomp_gen.py defs.json -o outdir/ [--prefix name] [--half]
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from typing import Dict, List, Optional, Set, Tuple

# Varying names/semantics shared with shader_recomp_gen.py (must match to link).
VARYINGS = [
    ("color0",    "float4", "COLOR0"),
    ("color1",    "float4", "COLOR1"),
    ("texcoord0", "float4", "TEXCOORD0"),
    ("texcoord1", "float4", "TEXCOORD1"),
    ("texcoord2", "float4", "TEXCOORD2"),
    ("texcoord3", "float4", "TEXCOORD3"),
    ("fog",       "float",  "TEXCOORD7"),
]

MAP_RGB = {
    "unsigned_identity": "saturate({x})",
    "unsigned_invert":   "(1.0 - saturate({x}))",
    "expand_normal":     "(2.0 * max({x}, 0.0) - 1.0)",
    "expand_negate":     "(1.0 - 2.0 * max({x}, 0.0))",
    "halfbias_normal":   "(max({x}, 0.0) - 0.5)",
    "halfbias_negate":   "(0.5 - max({x}, 0.0))",
    "signed_identity":   "({x})",
    "signed_negate":     "(-({x}))",
}

SCALE = {"identity": "{x}", "bias": "({x} - 0.5)", "x2": "({x} * 2.0)", "x2_bias": "(({x} - 0.5) * 2.0)",
         "x4": "({x} * 4.0)", "div2": "({x} * 0.5)"}

REG_LOCAL = {"zero": "float4(0.0, 0.0, 0.0, 0.0)", "fog": "fog", "v0": "v0", "v1": "v1",
             "t0": "t0", "t1": "t1", "t2": "t2", "t3": "t3", "r0": "r0", "r1": "r1",
             "v1r0_sum": "v1r0_sum", "ef_prod": "ef_prod"}
WRITABLE = {"r0", "r1", "t0", "t1", "t2", "t3", "v0", "v1"}


class GenError(Exception):
    pass


SAME_C = [False, False]              # set per def by generate()
CUBE_EXPR = None                     # None = real texCUBE; --cube normal|const|black substitute an expression
VARYINGS_AVAILABLE: Optional[Set[str]] = None   # --varyings: what the paired vertex program outputs


def src_expr(inp: dict, stage: Optional[dict], alpha: bool, final_c: Tuple[int, int] = (0, 0)) -> str:
    """Cg expression (float3 for rgb combiner, float for alpha) for one combiner input."""
    reg = inp["reg"]
    # Constants live per stage on the NV2A: stage i reads its own C0/C1 (PSConstant0/1[i]) unless the def
    # says SAME_C0/C1 (then every stage reads stage 0's).  Runtime uploads psc[i] = C0[i], psc[8+i] = C1[i],
    # psc[16..17] = final combiner constants; Halo never uses SetPixelShaderConstant, it rewrites the def.
    if reg == "c0":
        base = f"psc[{(0 if SAME_C[0] else stage['index']) if stage else 16}]"
    elif reg == "c1":
        base = f"psc[{(8 if SAME_C[1] else 8 + stage['index']) if stage else 17}]"
    elif reg in REG_LOCAL:
        base = REG_LOCAL[reg]
    else:
        raise GenError(f"unsupported input register {reg}")
    ch = inp["channel"]
    if alpha:
        comp = ".a" if ch == "alpha" else ".b"          # alpha combiner: alpha or blue
    else:
        comp = ".aaa" if ch == "alpha_rep" else ".rgb"  # rgb combiner: rgb or alpha replicate
    return MAP_RGB[inp["mapping"]].format(x=f"{base}{comp}")


def emit_stage(s: dict, mux_msb: bool, L: List[str], written: Set[str]) -> None:
    i = s["index"]
    ro, ao = s["rgb_out"], s["alpha_out"]
    L.append(f"    // ---- combiner stage {i}")
    L.append("    {")
    a, b, c, d = (src_expr(x, s, False) for x in s["rgb_in"])
    L.append(f"        float3 A = {a}, B = {b}, C = {c}, D = {d};")
    L.append(f"        float3 AB = {'float3(dot(A, B))' if ro['ab_dot'] else 'A * B'};")
    L.append(f"        float3 CD = {'float3(dot(C, D))' if ro['cd_dot'] else 'C * D'};")
    if ro["mux"]:
        sel = "(r0.a >= 0.5)" if mux_msb else "(fmod(floor(r0.a * 255.0 + 0.5), 2.0) >= 1.0)"
        L.append(f"        float3 SUM = {sel} ? CD : AB;")
    else:
        L.append("        float3 SUM = AB + CD;")
    aa, ab_, ac, ad = (src_expr(x, s, True) for x in s["alpha_in"])
    L.append(f"        float  a_A = {aa}, a_B = {ab_}, a_C = {ac}, a_D = {ad};")
    L.append("        float  a_AB = a_A * a_B, a_CD = a_C * a_D;")
    if ao["mux"]:
        sel = "(r0.a >= 0.5)" if mux_msb else "(fmod(floor(r0.a * 255.0 + 0.5), 2.0) >= 1.0)"
        L.append(f"        float  a_SUM = {sel} ? a_CD : a_AB;")
    else:
        L.append("        float  a_SUM = a_AB + a_CD;")

    def write(dst: str, expr: str, comps: str, scale: str) -> None:
        if dst == "zero" or dst not in WRITABLE:
            return
        e = SCALE[scale].format(x=expr)
        L.append(f"        {dst}.{comps} = clamp({e}, -1.0, 1.0);")
        written.add(dst)

    # rgb results
    write(ro["ab"], "AB", "rgb", ro["scale"])
    write(ro["cd"], "CD", "rgb", ro["scale"])
    write(ro["sum"], "SUM", "rgb", ro["scale"])
    # alpha results (blue-to-alpha overrides the alpha combiner's write for that register)
    if ro["ab_blue_to_alpha"] and ro["ab"] in WRITABLE:
        L.append(f"        {ro['ab']}.a = clamp({SCALE[ro['scale']].format(x='AB.b')}, -1.0, 1.0);")
    else:
        write(ao["ab"], "a_AB", "a", ao["scale"])
    if ro["cd_blue_to_alpha"] and ro["cd"] in WRITABLE:
        L.append(f"        {ro['cd']}.a = clamp({SCALE[ro['scale']].format(x='CD.b')}, -1.0, 1.0);")
    else:
        write(ao["cd"], "a_CD", "a", ao["scale"])
    write(ao["sum"], "a_SUM", "a", ao["scale"])
    L.append("    }")


def emit_textures(d: dict, L: List[str], samplers: Dict[int, str], warnings: List[str]) -> None:
    used = set(d["textures_used"])
    for t in d["textures"]:
        i = t["index"]
        mode = t["mode"]
        tc = f"IN.texcoord{i}"
        if VARYINGS_AVAILABLE is not None and f"texcoord{i}" not in VARYINGS_AVAILABLE:
            tc = "float4(0.0, 0.0, 0.0, 1.0)"          # vertex program never writes oT{i}
        if mode == "NONE":
            if i in used:
                warnings.append(f"t{i} read but stage mode NONE; sampling as PROJECT2D")
                samplers[i] = "sampler2D"
                L.append(f"    float4 t{i} = tex2D(tex{i}, {tc}.xy);")
            continue
        if mode == "PROJECT2D":
            samplers[i] = "sampler2D"
            L.append(f"    float4 t{i} = tex2D(tex{i}, {tc}.xy);")
        elif mode == "PROJECT3D":
            samplers[i] = "sampler2D"
            L.append(f"    float4 t{i} = tex2Dproj(tex{i}, float3({tc}.xy, {tc}.w));")
        elif mode == "CUBEMAP":
            if CUBE_EXPR is None:
                samplers[i] = "samplerCUBE"
                L.append(f"    float4 t{i} = texCUBE(tex{i}, {tc}.xyz);")
            else:
                warnings.append(f"t{i} CUBEMAP replaced by a stand-in expression")
                L.append(f"    float4 t{i} = {CUBE_EXPR.format(tc=tc)};")
        elif mode == "PASSTHRU":
            L.append(f"    float4 t{i} = saturate({tc});")
        elif mode == "CLIPPLANE":
            cm = t["compare_mode"]
            conds = []
            for bit, comp in ((1, "x"), (2, "y"), (4, "z"), (8, "w")):
                conds.append(f"{tc}.{comp} {'>=' if cm & bit else '<'} 0.0")
            L.append(f"    if (!({' && '.join(conds)})) discard;   // CLIPPLANE t{i}")
            L.append(f"    float4 t{i} = float4(0.0, 0.0, 0.0, 0.0);")
        elif mode in ("DPNDNT_AR", "DPNDNT_GB"):
            src = t["input_stage"] if t["input_stage"] is not None else max(0, i - 1)
            sw = "ar" if mode == "DPNDNT_AR" else "gb"
            samplers[i] = "sampler2D"
            L.append(f"    float4 t{i} = tex2D(tex{i}, t{src}.{sw});   // dependent lookup")
        elif mode in ("BUMPENVMAP", "BUMPENVMAP_LUM"):
            src = t["input_stage"] if t["input_stage"] is not None else max(0, i - 1)
            samplers[i] = "sampler2D"
            L.append(f"    float2 bump{i} = {tc}.xy + float2(dot(xv_bumpmat[{i}].xy, t{src}.rg),"
                     f" dot(xv_bumpmat[{i}].zw, t{src}.rg));")
            L.append(f"    float4 t{i} = tex2D(tex{i}, bump{i});")
            if mode == "BUMPENVMAP_LUM":
                L.append(f"    t{i}.rgb *= saturate(t{src}.b * xv_bumplum[{i}].x + xv_bumplum[{i}].y);")
        else:
            warnings.append(f"t{i}: texture mode {mode} not implemented; plain 2D fetch used")
            samplers[i] = "sampler2D"
            L.append(f"    float4 t{i} = tex2D(tex{i}, {tc}.xy);")


def generate(d: dict, name: str, use_half: bool) -> Tuple[str, List[str], Dict]:
    warnings: List[str] = list(d.get("warnings", []))
    samplers: Dict[int, str] = {}
    body: List[str] = []
    written: Set[str] = set()
    SAME_C[0] = bool(d.get("same_c0")); SAME_C[1] = bool(d.get("same_c1"))
    avail = VARYINGS_AVAILABLE
    def have(v: str) -> bool:
        return avail is None or v in avail

    body.append("    // ---- inputs (varyings the vertex program does not write read as NV2A defaults)")
    body.append(f"    float4 v0 = {'IN.color0' if have('color0') else 'float4(0.0, 0.0, 0.0, 0.0)'}, "
                f"v1 = {'IN.color1' if have('color1') else 'float4(0.0, 0.0, 0.0, 0.0)'};")
    body.append(f"    float4 fog = float4(xv_fogcolor.rgb, {'IN.fog' if have('fog') else '1.0'});")
    body.append("    float4 r0 = float4(0.0, 0.0, 0.0, 0.0), r1 = float4(0.0, 0.0, 0.0, 0.0);")
    # texture stages (declare all four locals so later references never dangle)
    declared = set()
    tex_lines: List[str] = []
    emit_textures(d, tex_lines, samplers, warnings)
    for ln in tex_lines:
        for i in range(4):
            if f"float4 t{i} =" in ln:
                declared.add(i)
    for i in range(4):
        if i not in declared:
            tex_lines.append(f"    float4 t{i} = float4(0.0, 0.0, 0.0, 0.0);")
    body += tex_lines

    for s in d["stages"]:
        emit_stage(s, d["mux_msb"], body, written)

    body.append("    // ---- final combiner")
    fin = d["final"]
    if fin["present"]:
        fc = (fin["c0_map"], fin["c1_map"])
        sum_expr = "v1.rgb + r0.rgb"
        if fin["complement_v1"]:
            sum_expr = "(1.0 - v1.rgb) + r0.rgb"
        if fin["complement_r0"]:
            sum_expr = sum_expr.replace("r0.rgb", "(1.0 - r0.rgb)")
        body.append(f"    float4 v1r0_sum = float4({'saturate(' + sum_expr + ')' if fin['clamp_sum'] else sum_expr}, 0.0);")
        e = src_expr(fin["e"], None, False, fc)
        f_ = src_expr(fin["f"], None, False, fc)
        body.append(f"    float4 ef_prod = float4({e} * {f_}, 0.0);")
        a = src_expr(fin["a"], None, False, fc)
        b = src_expr(fin["b"], None, False, fc)
        c = src_expr(fin["c"], None, False, fc)
        dd = src_expr(fin["d"], None, False, fc)
        g = src_expr(fin["g"], None, True, fc)
        body.append(f"    float3 FA = {a}, FB = {b}, FC = {c}, FD = {dd};")
        body.append("    float3 out_rgb = FA * FB + (1.0 - FA) * FC + FD;")
        body.append(f"    float  out_a   = {g};")
    else:
        body.append("    // (no final combiner in the def: Xbox D3D default = fixed-function fog blend)")
        body.append("    float3 out_rgb = lerp(fog.rgb, r0.rgb, fog.a);")
        body.append("    float  out_a   = r0.a;")
    body.append("    return saturate(float4(out_rgb, out_a));")

    # header + signature
    used_var = {"color0", "color1", "fog"} | {f"texcoord{i}" for i in range(4)
                                             if d["textures"][i]["mode"] != "NONE" or i in d["textures_used"]}
    if avail is not None:
        used_var = {v for v in used_var if v in avail}
    L = [
        "// ---------------------------------------------------------------------------",
        f"//  XboxVita Stage 3b - auto-generated Vita Cg fragment shader: {name}",
        f"//  source def: VA {d['va'] and ('0x%08X' % d['va'])}  file 0x{d['file_offset']:X}",
        f"//  {d['stage_count']} combiner stage(s){', MUX_MSB' if d['mux_msb'] else ''}; "
        f"textures {' '.join('t%d:%s' % (t['index'], t['mode']) for t in d['textures'] if t['mode'] != 'NONE') or '-'}",
        "//  Uniforms: psc[16] = SetPixelShaderConstant regs; xv_fogcolor.rgb; fog factor = TEXCOORD7",
    ]
    for s in d["stages"]:
        for line in s["text"]:
            L.append(f"//    {line}")
    for line in fin["text"]:
        L.append(f"//    {line}")
    if warnings:
        L += [f"//  ! {w}" for w in warnings]
    L.append("// ---------------------------------------------------------------------------")
    L.append("")
    L.append("struct VertOut")
    L.append("{")
    L.append("    float4 position    : POSITION;")
    for nm, ty, sem in VARYINGS:
        if nm in used_var:
            L.append(f"    {ty:<6} {nm:<11} : {sem};")
    L.append("};")
    L.append("")
    params = ["VertOut IN"]
    for i in sorted(samplers):
        params.append(f"uniform {samplers[i]} tex{i}")
    params.append("uniform float4 psc[18]")
    params.append("uniform float4 xv_fogcolor")
    if any(t["mode"] in ("BUMPENVMAP", "BUMPENVMAP_LUM") for t in d["textures"]):
        params.append("uniform float4 xv_bumpmat[4]")
        params.append("uniform float4 xv_bumplum[4]")
    L.append(f"float4 main({', '.join(params)}) : COLOR")
    L.append("{")
    L += body
    L.append("}")
    text = "\n".join(L) + "\n"
    if use_half:
        text = text.replace("float4 ", "half4 ").replace("float3 ", "half3 ").replace("float  ", "half  ")
    binding = {"name": name, "samplers": {f"tex{i}": samplers[i] for i in sorted(samplers)},
               "constants_used": d["constants_used"], "textures_used": d["textures_used"],
               "varyings": sorted(used_var), "warnings": warnings}
    return text, warnings, binding


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(prog="pixelshader_recomp_gen.py",
                                 description="NV2A register combiners -> Vita Cg fragment shaders (XboxVita Stage 3b).")
    ap.add_argument("json", help="dx8_pixelshader_parse.py --json output")
    ap.add_argument("-o", "--outdir", default="shaders")
    ap.add_argument("--prefix", default="ps", help="output name prefix (default ps -> ps_00.frag.cg ...)")
    ap.add_argument("--half", action="store_true", help="emit half precision (faster on USSE, less exact)")
    ap.add_argument("--binding-json", help="write sampler/constant binding manifest")
    ap.add_argument("--varyings", help="comma list of varyings the vertex program outputs (color0,color1,texcoord0..3,fog); "
                                       "others are not declared (GXM refuses fragment inputs the vertex program lacks)")
    ap.add_argument("--name", help="output name for a single def (instead of <prefix>_NN)")
    ap.add_argument("--cube", default="real", help="CUBEMAP handling: real (texCUBE) | normal (normalisation-cube stand-in) | const | black")
    args = ap.parse_args(argv)
    global VARYINGS_AVAILABLE, CUBE_EXPR
    if args.cube == "const": CUBE_EXPR = "float4(0.5, 0.5, 0.5, 1.0)"
    elif args.cube == "black": CUBE_EXPR = "float4(0.0, 0.0, 0.0, 1.0)"
    elif args.cube == "normal": CUBE_EXPR = "float4(normalize({tc}.xyz) * 0.5 + 0.5, 1.0)"
    if args.varyings is not None:
        VARYINGS_AVAILABLE = set(v for v in args.varyings.split(",") if v)

    with open(args.json, "r", encoding="utf-8") as f:
        doc = json.load(f)
    defs = doc.get("pixel_shaders", [])
    if not defs:
        print("error: no pixel_shaders in JSON", file=sys.stderr)
        return 1
    os.makedirs(args.outdir, exist_ok=True)
    bindings = []
    nwarn = 0
    for i, d in enumerate(defs):
        name = args.name if (args.name and len(defs) == 1) else f"{args.prefix}_{i:02d}"
        try:
            text, warnings, binding = generate(d, name, args.half)
        except GenError as e:
            print(f"error: {name}: {e}", file=sys.stderr)
            return 1
        path = os.path.join(args.outdir, f"{name}.frag.cg")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        nwarn += len(warnings)
        binding["cg"] = path
        bindings.append(binding)
        print(f"wrote {path}  ({d['stage_count']} stage(s), textures {binding['textures_used']}, "
              f"{len(warnings)} warning(s))")
    if args.binding_json:
        with open(args.binding_json, "w", encoding="utf-8") as f:
            json.dump(bindings, f, indent=1)
    print(f"{len(defs)} fragment shader(s), {nwarn} warning(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
