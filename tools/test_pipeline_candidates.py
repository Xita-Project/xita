#!/usr/bin/env python3
"""Benchmark admission/restoration with optional native pipeline candidates absent."""
from pathlib import Path
import os
import itertools
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="xita-pipeline-candidates-") as directory:
    output = Path(directory) / "benchmark"
    candidates=("OBJECT_POSE", "MATERIAL_PACKET", "POLYGON_EDGE", "CLIP_REGION")
    for disabled in itertools.product((False, True), repeat=len(candidates)):
        missing=[name for name, absent in zip(candidates,disabled) if absent]
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
