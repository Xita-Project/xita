#!/usr/bin/env python3
"""Prepare the pinned first post-intro draw privately; no draw is accepted here."""
import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import struct
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from recompiler.dx8_shader_parse import decode_function
from recompiler.dx8_pixelshader_parse import decode_psdef
from recompiler import shader_recomp_gen as vg
from recompiler import pixelshader_recomp_gen as pg

XBE_SHA = "03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d"
SNAPSHOT_SHA = "8ef2cb740df793e5de1e8c02345f82c7c76e21913c4fbbfa3d5c8ab856406d6f"
PROGRAM_SHA = "2727f7e1a0f6cf681176becd9173984abf04172758b725e3a7f02494ec2cf4ec"
PUSH_SHA = "26f20ede5de38d1e4e10fe319bc2125865465b11042ace5fc735563975ad2d4e"
TEXTURE_SHA = "28b94c7f7595c9a970886a071d9a20ca9f87c6bb24e82f4ecf03a226ca0afab2"
COPY_FRAGMENT = """// Private staging initialization: copy all original destination channels.
struct VertOut { float4 position : POSITION; float4 texcoord0 : TEXCOORD0; };
float4 main(VertOut IN, uniform sampler2D copy_source) : COLOR
{
    return tex2Dproj(copy_source, float3(IN.texcoord0.xy / float2(640.0, 480.0), IN.texcoord0.w));
}
"""


def pixel_definition(setup):
    """Reconstruct decoder input from retained hardware banks, not XDK handles."""
    def bank(method, count=8):
        return setup[method // 4:method // 4 + count]
    words = (bank(0x260) + bank(0x288, 2) + bank(0xA60) + bank(0xA80) +
             bank(0xAA0) + bank(0xAC0) + bank(0x17F8, 1) + bank(0x1E20, 2) +
             bank(0x1E40) + bank(0x1E60, 1) + bank(0x1E70, 2) +
             [0, 0x76543210, 0x76543210, 0])
    if len(words) != 60:
        raise ValueError("invalid combiner bank shape")
    return asdict(decode_psdef(struct.pack("<60I", *words), 0))


def vertex_source(program):
    function = asdict(decode_function(struct.pack("<HH", 0x2078, 7) + program, 0))
    if function["warnings"] or function["decoded_count"] != 7:
        raise ValueError("unexpected vertex decode")
    declaration = {"attributes": [dict(vreg=v, type="FLOAT4", stream=0,
                                        offset=v * 16, size=16) for v in range(7)]}
    args = SimpleNamespace(mode="function", keep_viewport_epilogue=True, const_count=None)
    source, plan = vg.generate(declaration, function, args, "H2 captured seven-slot screen program")
    if plan.warnings or plan.skip_slots:
        raise ValueError("unexpected vertex translation plan")
    assignments = [line for line in source.splitlines() if line.startswith("    OUT.position = ")]
    if len(assignments) != 1 or "oPos.z * 0.9999" not in assignments[0]:
        raise ValueError("unexpected shared position output")
    # The original program writes window coordinates. Keep all original slots;
    # only adapt its final screen-space output to GXM clip coordinates. The
    # eventual consumer must require 640x480 and clip maximum 16777215.
    source = source.replace(assignments[0], """    float2 screen = sign(oPos.xy) * floor(abs(oPos.xy) * 16.0) / 16.0;
    OUT.position = float4((2.0 * screen.x / 640.0 - 1.0) * oPos.w,
                          (1.0 - 2.0 * screen.y / 480.0) * oPos.w,
                          (oPos.z / 16777215.0) * oPos.w, oPos.w);""")
    details = asdict(plan)
    details["helpers"] = sorted(details["helpers"])
    details["skip_slots"] = sorted(details["skip_slots"])
    return source, details


def fragment_source(d):
    # This explicit route is H2-only. Do not alter the shared generator's
    # default unsupported HILO behavior or any CE texture-coordinate policy.
    modes = [(t["mode"], t["dot_mapping"]) for t in d["textures"]]
    if (d["warnings"] or modes != [("PROJECT2D", 0), ("DOTPRODUCT", 4), ("DOT_ST", 4), ("NONE", 0)] or
            d["textures"][2]["input_stage"] != 0 or d["stage_count"] != 4 or
            d["same_c0"] or d["same_c1"] or d["mux_msb"]):
        raise ValueError("unsupported screen texture/combiner route")
    fin = d["final"]
    if (not fin["present"] or fin["d"]["reg"] != "r0" or fin["g"]["reg"] != "r0" or
            fin["d"]["mapping"] != "unsigned_identity" or fin["g"]["mapping"] != "unsigned_identity" or
            fin["d"]["channel"] != "rgb" or fin["g"]["channel"] != "alpha" or
            any(fin[k]["reg"] != "zero" or fin[k]["mapping"] != "unsigned_identity" for k in "abcef")):
        raise ValueError("unsupported screen final combiner")
    allowed = {"zero", "c0", "c1", "r0", "r1", "t0", "t2"}
    if any(inp["reg"] not in allowed for st in d["stages"] for inp in st["rgb_in"] + st["alpha_in"]):
        raise ValueError("unbound live screen input")
    if any(st[k][out] not in {"zero", "r0", "r1"} for st in d["stages"]
           for k in ("rgb_out", "alpha_out") for out in ("ab", "cd", "sum")):
        raise ValueError("unsupported screen combiner output")
    body = ["""// H2-only pinned screen effect. A point-sampled ARGB byte image feeds
// unsigned HILO_1: high=(A<<8)|R, low=(G<<8)|B, both normalized by 65535.
// This is the pinned xemu interpretation, not proof of all NV2A dot mappings.
struct VertOut { float4 position : POSITION; float4 texcoord0 : TEXCOORD0;
    float4 texcoord1 : TEXCOORD1; float4 texcoord2 : TEXCOORD2; };
float4 main(VertOut IN, uniform sampler2D tex0, uniform sampler2D tex2,
            uniform float4 psc[18]) : COLOR
{
    float4 t0 = tex2Dproj(tex0, float3(IN.texcoord0.xy / float2(640.0, 480.0), IN.texcoord0.w));
    // Point UNORM8 sampling has integral source bytes: rounding recovers those
    // bytes despite host float representation. Filtered HILO input is excluded.
    float4 bytes0 = floor(saturate(t0) * 255.0 + 0.5);
    float3 hilo = float3(dot(bytes0.ar, float2(256.0, 1.0)) / 65535.0,
                        dot(bytes0.gb, float2(256.0, 1.0)) / 65535.0, 1.0);
    float dot1 = dot(IN.texcoord1.xyz, hilo);
    float dot2 = dot(IN.texcoord2.xyz, hilo);
    float4 t2 = tex2D(tex2, float2(dot1, dot2));
    float4 r0 = float4(0.0, 0.0, 0.0, t0.a), r1 = float4(0.0, 0.0, 0.0, 0.0);"""]
    saved = pg.SAME_C[:]
    try:
        pg.SAME_C[:] = [False, False]
        written = set()
        for stage in d["stages"]:
            pg.emit_stage(stage, False, body, written)
    finally:
        pg.SAME_C[:] = saved
    body += ["    return saturate(r0);", "}"]
    return "\n".join(body) + "\n"


def probe_inputs(push, texture):
    raw = push.read_bytes()
    blocks = texture.read_bytes()
    if hashlib.sha256(raw).hexdigest() != PUSH_SHA or hashlib.sha256(blocks).hexdigest() != TEXTURE_SHA:
        raise ValueError("requires pinned native181 probe inputs")
    base, size, put, reserved = struct.unpack_from("<4I", raw)
    if len(raw) != 16 + size or reserved or put != 0x03B80158:
        raise ValueError("unexpected captured push layout")
    cursor = 16 + 0x03B7B420 - base
    def packet(method, count):
        nonlocal cursor
        header = struct.unpack_from("<I", raw, cursor)[0]
        if header != (count << 18) | method:
            raise ValueError("unexpected original immediate ordering")
        values = struct.unpack_from(f"<{count}I", raw, cursor + 4)
        cursor += 4 * (count + 1)
        return values
    if packet(0x17FC, 1) != (7,):
        raise ValueError("unexpected topology")
    vertices = []
    for _ in range(4):
        # v5/v6 feed only dead color outputs in this fragment pipeline. These
        # probe-only values are zero; no claim of captured current color state.
        registers = [0] * 28
        for register, method in ((4, 0x1A40), (3, 0x1A30), (2, 0x1A20), (1, 0x1A10), (0, 0x1518)):
            registers[register * 4:register * 4 + 4] = packet(method, 4)
        vertices.extend(registers)
    if packet(0x17FC, 1) != (0,):
        raise ValueError("unexpected quad end")
    if struct.unpack_from("<8I", blocks) != (1, 0x018FA680, 8, 8, 32, 64, 0x03310E29, 1):
        raise ValueError("unexpected compressed capture")
    return {"screen.vertices.bin": struct.pack("<112I", *vertices),
            "screen.texture2.bin": blocks[32:]}


def prepare(xbe, snapshot, out, push=None, texture=None):
    if hashlib.sha256(xbe.read_bytes()).hexdigest() != XBE_SHA:
        raise ValueError("owned XBE revision mismatch")
    if hashlib.sha256(snapshot.read_bytes()).hexdigest() != SNAPSHOT_SHA:
        raise ValueError("requires exact native180/181 begin snapshot")
    state = json.loads(snapshot.read_text())
    program = struct.pack("<28I", *(w for row in state["program"][:7] for w in row))
    if hashlib.sha256(program).hexdigest() != PROGRAM_SHA:
        raise ValueError("original program mismatch")
    vertex, plan = vertex_source(program)
    pixel = pixel_definition(state["setup"])
    fragment = fragment_source(pixel)
    factors = state["setup"][0xA60 // 4:0xAA0 // 4] + state["setup"][0x1E20 // 4:0x1E28 // 4]
    floats = [(word >> shift & 255) / 255 for word in factors for shift in (16, 8, 0, 24)]
    artifacts = {"screen.vert.cg": vertex.encode(), "screen.frag.cg": fragment.encode(),
                 "screen.copy.frag.cg": COPY_FRAGMENT.encode(),
                 "screen.constants.bin": struct.pack("<72f", *floats),
                 "screen.contract.bin": struct.pack("<II", 0x43533248, 1) +
                    struct.pack("<2048I", *state["setup"]) +
                    struct.pack("<64I", *state["setup_valid"]) + program}
    if (push is None) != (texture is None):
        raise ValueError("probe requires both push and texture captures")
    if push is not None:
        artifacts.update(probe_inputs(push, texture))
        artifacts["screen.contract.bin"] = (struct.pack("<II", 0x43533248, 2) +
            artifacts["screen.contract.bin"][8:] + artifacts["screen.vertices.bin"])
    out.mkdir(parents=True, exist_ok=True)
    for name, data in artifacts.items():
        (out / name).write_bytes(data)
    report = dict(scope="private first post-intro screen-effect preparation; no accepted draw",
                  xbe_sha256=XBE_SHA, snapshot_sha256=SNAPSHOT_SHA, program_sha256=PROGRAM_SHA,
                  vertex_plan=plan, pixel_definition=pixel,
                  files={k: hashlib.sha256(v).hexdigest() for k, v in artifacts.items()})
    (out / "screen-shaders.json").write_text(json.dumps(report, indent=2) + "\n")
    return report["files"]


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("xbe", type=Path)
    ap.add_argument("snapshot", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--push-capture", type=Path)
    ap.add_argument("--texture-capture", type=Path)
    args = ap.parse_args()
    print(json.dumps(prepare(args.xbe, args.snapshot, args.out, args.push_capture, args.texture_capture), indent=2))
