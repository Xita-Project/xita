#!/usr/bin/env python3
"""Check 32-bit DIV/IDIV against an independent integer-magnitude oracle."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="xita-div32-") as directory:
    executable = Path(directory) / "test"
    subprocess.run([
        os.environ.get("CC", "cc"), "-O2", "-std=gnu11", "-fno-strict-aliasing",
        *shlex.split(os.environ.get("DIV32_TEST_CFLAGS", "")),
        "-I" + str(root / "recomp"), str(root / "recomp/host/div32_test.c"),
        "-lm", "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
