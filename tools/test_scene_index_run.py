#!/usr/bin/env python3
"""Compare the experimental index prefix against the owned retained loop.

Generated reference code is written only to the caller's private output folder.
No production hook is installed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--reference", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    source = args.reference.read_text()
    function = source.split("void f_00054010(xctx *restrict c)\n", 1)[1].split("\nvoid f_", 1)[0]
    loop = function.split("L_00054132:\n", 1)[1].split("L_00054141:\n", 1)[0]
    expected = "a695e16fa083c16448f0492c4f3de2ac78c640313483e758b418e15bea200d4e"
    if hashlib.sha256(loop.encode()).hexdigest() != expected:
        raise SystemExit("unsupported retained loop")
    args.out.mkdir(parents=True, exist_ok=False)
    text = '#include "kernel/xk_scene_index_run.h"\nextern unsigned batches;\n'
    for name in ("original", "candidate"):
        text += f"void {name}(xctx *restrict c) {{\nL_00054132:\n"
        if name == "candidate":
            text += "batches += !!xv_scene_index_run(c,g_xram,g_xpt,65536);\n"
        text += loop + "L_00054141: return;\n}\n"
    reference = args.out / "reference.c"
    reference.write_text(text)
    cmd = [os.environ.get("CC", "cc"), "-O2", "-g", "-fno-strict-aliasing",
           "-fsanitize=address,undefined", "-no-pie", "-I" + str(ROOT / "recomp"),
           str(reference), str(ROOT / "tools/tests/scene_index_run.c"),
           "-o", str(args.out / "test")]
    (args.out / "command.json").write_text(json.dumps(cmd, indent=2) + "\n")
    subprocess.run(cmd, check=True)
    subprocess.run([str(args.out / "test")], check=True)


if __name__ == "__main__":
    main()
