#!/usr/bin/env python3
"""Compare the experimental index prefix against the owned retained loop.

Generated reference code is written only to the caller's private output folder.
Use --hook to exercise the opt-in production admission and routing too.
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
    ap.add_argument("--hook", action="store_true", help="also exercise helper admission and verify/fast routing")
    ap.add_argument("--global-list", action="store_true", help="exercise the bounded scene-global virtual list")
    args = ap.parse_args()
    source = args.reference.read_text()
    function = source.split("void f_00054010(xctx *restrict c)\n", 1)[1].split("\nvoid f_", 1)[0]
    loop = function.split("L_00054132:\n", 1)[1].split("L_00054141:\n", 1)[0]
    expected = "a695e16fa083c16448f0492c4f3de2ac78c640313483e758b418e15bea200d4e"
    if hashlib.sha256(loop.encode()).hexdigest() != expected:
        raise SystemExit("unsupported retained loop")
    args.out.mkdir(parents=True, exist_ok=False)
    text = '#include "kernel/xk_scene_index_run.h"\nextern unsigned batches;\n'
    if args.hook:
        text += '#include "kernel/xk_scene_index_hook.h"\n'
        text += 'uint32_t xv_scene_index_stack(const void *c) {(void)c; return 0x1000;}\n'
        text += 'unsigned xk_mem_arena_size(void) {return 65536;}\n'
    for name in ("original", "candidate"):
        text += f"void {name}(xctx *restrict c) {{\n"
        if name == "candidate" and args.hook:
            text += "xv_scene_index_scope scope; xv_scene_index_begin(&scope,c);\n"
        text += "L_00054132:\n"
        if name == "candidate":
            if args.hook:
                text += "{uint32_t old=c->r[3]; xv_scene_index_step(&scope,c,g_xram,g_xpt); batches += scope.pending != 0 || old != c->r[3];}\n"
            else:
                text += "batches += !!xv_scene_index_run(c,g_xram,g_xpt,65536);\n"
        text += loop + "L_00054141:;\n"
        if name == "candidate" and args.hook:
            text += 'xv_scene_index_end_run(&scope); if(scope.mode != atoi(getenv("XV_SCENE_INDEX_RUN"))) abort();\n'
        text += "return;\n}\n"
    reference = args.out / "reference.c"
    reference.write_text(text)
    fixture = ROOT / "tools/tests/scene_index_run.c"
    if args.global_list:
        original_fixture = fixture.read_text()
        replacements = {
            "{0x1800, 0x1ff0, 0x1ffc, 0x2000}[k % 4]": "{0x38be14, 0x38bff0, 0x38bffc, 0x38c000}[k % 4]",
            "g_xpt[2] = 0x9000;": "g_xpt[0x38c] = 0x9000;",
            "g_xpt[i] = i * 4096u;": "g_xpt[i] = i * 4096u;\n    g_xpt[0x38b] = 0x1000; g_xpt[0x38c] = 0x2000;",
        }
        for old, new in replacements.items():
            if original_fixture.count(old) != 1:
                raise SystemExit("global-list fixture layout drift")
            original_fixture = original_fixture.replace(old, new)
        fixture = args.out / "global_fixture.c"
        fixture.write_text(original_fixture)
    cmd = [os.environ.get("CC", "cc"), "-O2", "-g", "-fno-strict-aliasing",
           "-fsanitize=address,undefined", "-no-pie", "-I" + str(ROOT / "recomp"),
           str(reference), str(fixture),
           "-o", str(args.out / "test")]
    (args.out / "command.json").write_text(json.dumps(cmd, indent=2) + "\n")
    subprocess.run(cmd, check=True)
    for mode in (("1", "2") if args.hook else ("0",)):
        env = dict(os.environ, XV_SCENE_INDEX_RUN=mode)
        subprocess.run([str(args.out / "test")], check=True, env=env)


if __name__ == "__main__":
    main()
