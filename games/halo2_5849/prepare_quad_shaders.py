#!/usr/bin/env python3
"""Prepare the observed H2 movie-quad shaders privately; no guest draw hook."""
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
from recompiler.xita_recomp import Image
from recompiler.dx8_shader_parse import decode_function
from recompiler import shader_recomp_gen as gen

XBE_SHA = "03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d"
PROGRAM_SHA = "2e4131cffc816b0306d02238b90d3e4fb0c31ae4ce982ffa2c9980f9368260da"
CONTRACT_SNAPSHOT_SHA = "a008f536b05e8e3aebcea56d148da630b0cd302f8d74797417b76d673a4bf659"

# This specialization requires the exact two-stage combiner documented in
# halo2-quad-gxm-probe.md. It is not a general pixel shader translation.
FRAGMENT = """// H2 observed combiner: live t0*c0[0]*diffuse; c0[0] must be white.
// Other stage outputs are dead; there are no invented t1/t2 samples.
struct VertOut { float4 position : POSITION; float4 color0 : COLOR0; float4 texcoord0 : TEXCOORD0; };
float4 main(VertOut IN, uniform sampler2D tex0, uniform float4 h2_tex_scale) : COLOR
{
    float4 sample0 = tex2Dproj(tex0, float3(IN.texcoord0.xy * h2_tex_scale.xy, IN.texcoord0.w));
    return sample0 * IN.color0;
}
"""


def vertex_source(function):
    # Immediate inputs are explicitly expanded by the future consumer. These
    # are vertex-register bindings, not inferred CE declaration semantics.
    declaration = {"attributes": [dict(vreg=v, type="FLOAT4", stream=0,
                                        offset=i * 16, size=16)
                                   for i, v in enumerate((0, 3, 9))]}
    args = SimpleNamespace(mode="function", keep_viewport_epilogue=True, const_count=None)
    source, plan = gen.generate(declaration, function, args, "H2 owned XVS 0x43F2B0")
    if plan.warnings or plan.skip_slots or plan.c_base != -86 or plan.c_count != 178:
        raise ValueError("unexpected H2 shader translation plan")
    # Keep the shared CE generator unchanged. Fail closed if its output contract
    # changes. Only the generated output initialization and final host projection
    # differ; all 21 original arithmetic slots, including the dual-issued RCC,
    # remain in their original order.
    signature = "VertOut main(AppIn IN, uniform float4 c[178])"
    if source.count(signature) != 1:
        raise ValueError("unexpected generated shader signature")
    source = source.replace(signature, signature[:-1] + ", uniform float4 h2_surface)")
    for output in plan.outputs:
        if output.oreg == "oPos":
            continue
        old = output.oreg + " = float4(0.0, 0.0, 0.0, 0.0)"
        if source.count(old) != 1:
            raise ValueError("unexpected generated output initialization")
        source = source.replace(old, output.oreg + " = float4(0.0, 0.0, 0.0, 1.0)")
    assignments = [line for line in source.splitlines() if line.startswith("    OUT.position = ")]
    if len(assignments) != 1 or "oPos.z * 0.9999" not in assignments[0]:
        raise ValueError("unexpected shared position output contract")
    source = source.replace(assignments[0], """    float2 screen = sign(oPos.xy) * floor(abs(oPos.xy) * 16.0) / 16.0;
    OUT.position = float4((2.0 * screen.x / h2_surface.x - 1.0) * oPos.w,
                          (1.0 - 2.0 * screen.y / h2_surface.y) * oPos.w,
                          (oPos.z / h2_surface.z) * oPos.w, oPos.w);""")
    return source + "\n", plan


def prepare(xbe, snapshot, out, contract=False):
    if hashlib.sha256(xbe.read_bytes()).hexdigest() != XBE_SHA:
        raise ValueError("owned XBE revision mismatch")
    image = Image(str(xbe))
    program = image.bytes_at(0x43F2B0, 340)
    if hashlib.sha256(program).hexdigest() != PROGRAM_SHA:
        raise ValueError("owned vertex program mismatch")
    function = decode_function(program, 0)
    if function.warnings or function.decoded_count != 21:
        raise ValueError("vertex decode mismatch")
    source, plan = vertex_source(asdict(function))
    artifacts = {"quad.vert.cg": source.encode(), "quad.frag.cg": FRAGMENT.encode(),
                 "quad.program.bin": program[4:]}
    if snapshot:
        state = json.loads(snapshot.read_text())
        words = struct.unpack("<84I", program[4:])
        captured = [word for row in state["program"][:21] for word in row]
        constants = state["constants"]
        if list(words) != captured or len(constants) != 192 or any(len(row) != 4 for row in constants):
            raise ValueError("snapshot program/constant shape mismatch")
        artifacts["constants.bin"] = struct.pack("<712I", *(word for row in constants[10:188] for word in row))
    if contract:
        if not snapshot or hashlib.sha256(snapshot.read_bytes()).hexdigest() != CONTRACT_SNAPSHOT_SHA:
            raise ValueError("quad contract requires the pinned native73 pipeline capture")
        artifacts["quad.contract.bin"] = (struct.pack("<II", 0x43513248, 1) +
            struct.pack("<2048I", *state["setup"]) +
            struct.pack("<64I", *state["setup_valid"]) + artifacts["constants.bin"] + program[4:])
    out.mkdir(parents=True, exist_ok=True)
    for name, contents in artifacts.items():
        (out / name).write_bytes(contents)
    manifest = {"scope": "private pinned movie-quad renderer contract" if contract else "private observed movie-quad shader probe",
                "xbe_sha256": XBE_SHA, "vertex_program_sha256": PROGRAM_SHA,
                "snapshot_sha256": hashlib.sha256(snapshot.read_bytes()).hexdigest() if snapshot else None,
                "constant_hardware_base": 10, "constant_count": plan.c_count,
                "expanded_vregs": [0, 3, 9], "vertex_slots": 21,
                "files": {name: hashlib.sha256(data).hexdigest() for name, data in artifacts.items()}}
    (out / "quad-shaders.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("xbe", type=Path)
    parser.add_argument("--snapshot", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--contract", action="store_true", help="emit only the pinned native73 draw contract")
    args = parser.parse_args()
    print(json.dumps(prepare(args.xbe, args.snapshot, args.out, args.contract), indent=2))
