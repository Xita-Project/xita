#!/usr/bin/env python3
"""Benchmark admission/restoration with either native pipeline candidate absent."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="xita-pipeline-candidates-") as directory:
    output = Path(directory) / "benchmark"
    for missing in ((), ("OBJECT_POSE",), ("MATERIAL_PACKET",),
                    ("OBJECT_POSE", "MATERIAL_PACKET")):
        subprocess.run([
            os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
            "-fno-omit-frame-pointer", "-no-pie",
            "-DXV_NATIVE_OBJECT_BASIS", "-DXV_NATIVE_MODEL_PALETTE",
            *("-DTEST_NO_" + name for name in missing),
            str(ROOT / "recomp/host/resolution_benchmark_test.c"),
            "-lm", "-o", str(output),
        ], check=True)
        subprocess.run([str(output)], check=True)
    print("PASS: candidate availability; off/on/off completion, cancellation and lost-view restoration; absent optional APIs")
