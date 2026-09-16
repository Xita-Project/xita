#!/usr/bin/env python3
"""tools/h2_menu_shaders.py - Halo 2 menu shader pipeline (GXM backend inputs).

Reads the "[h2/menu-shader]" inventory lines a menu run logs (one per distinct vertex
program / combiner pair, with the combiner registers) and the private vertex-program
dumps (menu-vp-<hash>.bin: start, length, 16-byte NV2A slots) and writes, per program:
  h2menu_ps_<hash>.frag.cg (+ .json)   combiner -> Cg fragment shader (recompiler/pixelshader_recomp_gen)
  h2menu_vs_<hash>.cg      (+ .json)   NV2A microcode -> Cg vertex shader (recompiler/shader_recomp_gen)
Compile the .cg files with tools/shadercomp (libshacccg on a Vita or under Vita3K) into
local/halo2_5849/menu-shaders/ and rebuild; games/halo2_5849/menu_gxm.c loads them by hash.
No game bytes are embedded: inputs are register words and the game's own microcode dumps,
which stay private (local/, ux0:data) and are not committed.

usage: h2_menu_shaders.py <boot.log> <vp-dump-dir> <outdir>
"""
import argparse, glob, json, os, re, struct, sys
from dataclasses import asdict
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "recompiler"))
import dx8_pixelshader_parse as pp
import pixelshader_recomp_gen as gen
import dx8_shader_parse as s2
import shader_recomp_gen as s3

INVENTORY = re.compile(r"\[h2/menu-shader\] pair#(\d+) vp=([0-9a-f]+) ps=([0-9a-f]+) prim=(\d+) vp_len=(\d+)(?: vp_start=(\d+))? stages=(\d+) "
                       r"final=([0-9A-F]+)/([0-9A-F]+)/([0-9A-F]+)/([0-9A-F]+) stagectl=([0-9A-F]+)(?: ctl=([0-9A-F]+) cmp=([0-9A-F]+) dot=([0-9A-F]+) inp=([0-9A-F]+))? tex=([0-9A-F]+)((?: [0-9A-F/]+)+)")


VS_VARYING = {"oD0": "color0", "oD1": "color1", "oFog": "fog", "oT0": "texcoord0", "oT1": "texcoord1",
              "oT2": "texcoord2", "oT3": "texcoord3"}


def fragment_programs(log, outdir, vs_outputs):
    """One fragment shader per (combiner, vertex program) pair: GXM links the fragment program
    against the vertex program, so the shader may only read varyings that program writes."""
    seen = {}
    gen.NONE_STAGE_ZERO = True      # a NONE stage reads as zero (the menu text program sums t2*c0 + t0*v0)
    gen.TEXCOORD_SCALE = True       # linear images are texel-addressed: runtime sets xv_texscale[i]
    for line in open(log, errors="replace"):
        m = INVENTORY.search(line)
        if not m or (m.group(3), m.group(2)) in seen:
            continue
        ps, vp = m.group(3), m.group(2)
        gen.VARYINGS_AVAILABLE = vs_outputs.get(vp)          # None (all) if the VP dump is missing
        stages = int(m.group(7)); fabcd, fefg, fc0, fc1 = (int(m.group(i), 16) for i in (8, 9, 10, 11))
        texmodes = int(m.group(12), 16)
        ctl = int(m.group(13), 16) if m.group(13) else (stages | 0x1000 | 0x10000)   # UNIQUE_C0/C1 until logged
        cmp_ = int(m.group(14), 16) if m.group(14) else 0
        dot = int(m.group(15), 16) if m.group(15) else 0
        inp = int(m.group(16), 16) if m.group(16) else 0
        words = [w for w in m.group(18).split() if "/" in w][:stages]
        st = [[int(x, 16) for x in w.split("/")] for w in words]   # rgb_in, rgb_out, alpha_in, alpha_out, c0, c1
        pad = lambda lst: (lst + [0] * 8)[:8]
        cmap = int("".join(f"{i:X}" for i in range(7, -1, -1)), 16)
        blob = struct.pack(pp.PSDEF_FMT, *pad([s[2] for s in st]), fabcd, fefg, *pad([s[4] for s in st]), *pad([s[5] for s in st]),
                           *pad([s[3] for s in st]), *pad([s[0] for s in st]), cmp_, fc0, fc1, *pad([s[1] for s in st]),
                           ctl, texmodes, dot, inp, cmap, cmap, 0x98)
        d = asdict(pp.decode_psdef(blob, 0))
        name = f"h2menu_ps_{ps}_{vp}"
        src, warns, binding = gen.generate(d, name, False)
        open(os.path.join(outdir, name + ".frag.cg"), "w").write(src)
        json.dump({"name": name, "ps_hash": ps, "vp_hash": vp, "stages": stages, "texmodes": texmodes, "binding": binding,
                   "varyings": sorted(vs_outputs.get(vp) or []), "warnings": warns},
                  open(os.path.join(outdir, name + ".json"), "w"), indent=1, default=str)
        seen[(ps, vp)] = name
        print(f"{name}: stages={stages} texmodes=0x{texmodes:08X} varyings={sorted(vs_outputs.get(vp) or ['*'])} warnings={len(warns)}")
    return len(seen)


def vertex_programs(vpdir, outdir, vs_outputs):
    args = argparse.Namespace(mode="function", rhw=False, light=False, const_count=192, keep_viewport_epilogue=False,
                              binding_json=None, stdout=False, output=None, json=None, c_base=0)
    n_ok = 0
    for f in sorted(glob.glob(os.path.join(vpdir, "menu-vp-*.bin"))):
        d = open(f, "rb").read(); start, n = struct.unpack("<2I", d[:8]); n = min(n, 136)
        instrs = []
        for i in range(n):
            ins = s2.decode_instruction(struct.unpack_from("<4I", d, 8 + 16 * i), i, 8 + 16 * i, 0)   # hardware c[] numbering
            instrs.append(ins)
            if ins.final: break
        vin, vout, temps, consts, warns = set(), set(), set(), set(), []
        for ins in instrs:
            for op in ins.ops:
                for o in op.inputs:
                    if o.kind == "v": vin.add(o.index)
                    elif o.kind == "r": temps.add(o.index)
                    elif o.kind == "c": consts.add(o.index)
                for o in op.outputs:
                    if o.kind == "o": vout.add(o.name)
                    elif o.kind == "r": temps.add(o.index)
            warns.extend(f"slot {ins.slot}: {w}" for w in ins.warnings)
        func = s2.ShaderFunction(file_offset=0, type_code=1, type_name="vertex", version_code=0x20, version_name="xvs1.1",
                                 declared_count=len(instrs), decoded_count=len(instrs), byte_size=16 * len(instrs), const_bias=0,
                                 instructions=instrs, inputs_used=[f"v{i}" for i in sorted(vin)], outputs_used=sorted(vout),
                                 temps_used=[f"r{i}" for i in sorted(temps)], constants_used=sorted(consts), warnings=warns)
        attrs = [{"vreg": v, "semantic": s2.VREG_SEMANTIC.get(v, ""), "stream": 0, "offset": 16 * v, "size": 16,
                  "type": "FLOAT4", "type_code": 0x42, "gxm": "F32 x4"} for v in sorted(vin)]
        decl = {"file_offset": None, "token_count": 0, "byte_size": 0, "tokens": [], "streams": {"0": 256},
                "attributes": attrs, "warnings": []}
        name = os.path.basename(f)[:-4].replace("menu-vp-", "h2menu_vs_")
        vs_outputs[name[len("h2menu_vs_"):]] = {VS_VARYING[o] for o in vout if o in VS_VARYING}
        text, plan = s3.generate(decl, asdict(func), args, name)
        # Halo 2's vertex programs keep the Xbox viewport epilogue and output WINDOW coordinates
        # (w = clip w); undo the 640x480 viewport so GXM's (320,320,240,-240, zScale 1) reproduces it.
        old_out = "OUT.position = float4(oPos.xy, oPos.z * 0.9999, oPos.w);"
        assert old_out in text, name + ": position epilogue line not found"
        text = text.replace(old_out, "OUT.position = float4((oPos.x * (1.0 / 320.0) - 1.0) * oPos.w, (1.0 - oPos.y * (1.0 / 240.0)) * oPos.w, "
                                     "(oPos.z * (0.9999 / 16777215.0)) * oPos.w, oPos.w);   /* window -> clip (Xbox VP epilogue output) */")
        open(os.path.join(outdir, name + ".cg"), "w").write(text + "\n")
        json.dump(s3.binding_manifest(plan, name + ".cg"), open(os.path.join(outdir, name + ".json"), "w"), indent=1, default=str)
        n_ok += 1
        print(f"{name}: slots={len(instrs)} start={start} inputs={sorted(vin)} outputs={sorted(vout)} warnings={len(plan.warnings)}")
    return n_ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("log"); ap.add_argument("vpdir"); ap.add_argument("outdir")
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    vs_outputs = {}
    nv = vertex_programs(a.vpdir, a.outdir, vs_outputs); nf = fragment_programs(a.log, a.outdir, vs_outputs)
    print(f"{nf} fragment + {nv} vertex programs -> {a.outdir}")


if __name__ == "__main__":
    main()
