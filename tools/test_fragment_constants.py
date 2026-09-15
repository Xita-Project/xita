#!/usr/bin/env python3
"""Fault-inject the production mesh fragment-uniform submission helper."""
import os
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
source = (root / "runtime/xv_d3d.c").read_text()
start = source.index("static int bind_fragment_constants(")
end = source.index("static void render_range(", start)
with tempfile.TemporaryDirectory(prefix="xita-fragment-constants-") as tmp:
    tmp = pathlib.Path(tmp)
    (tmp / "fragment_constants.inc").write_text(source[start:end])
    exe = tmp / "test"
    subprocess.run([
        "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-I", str(tmp),
        str(root / "tools/tests/fragment_constants.c"), "-o", str(exe)
    ], check=True)
    for override in ("0", "1"):
        subprocess.run([str(exe)], check=True,
                       env={**os.environ, "XV_NO_ATEST": override})
