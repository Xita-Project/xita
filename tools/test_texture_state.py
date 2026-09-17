#!/usr/bin/env python3
"""Replay deterministic binding traces against the production descriptor cache."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sdk = Path(os.environ.get("VITASDK", str(Path.home() / "vitasdk")))
source = (root / "runtime/xv_d3d.c").read_text()
start = source.index("#ifndef XV_TEXTURE_STATE_CACHE_DEFAULT")
end = source.index("/* Dashboard/environment handoff", start)
with tempfile.TemporaryDirectory(prefix="xita-texture-state-") as directory:
    (Path(directory) / "texture_config.inc").write_text(source[start:end])
    exe = Path(directory) / "test"
    subprocess.run(["cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-no-pie", "-fno-omit-frame-pointer",
                    "-I", str(root / "runtime"), "-I", directory, "-idirafter", str(sdk / "arm-vita-eabi/include"),
                    str(root / "tools/tests/texture_state.c"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    for configured in (None, "0", "1", "invalid", "-1"):
        env = dict(os.environ)
        env.pop("XV_TEXTURE_STATE_CACHE", None)
        if configured is not None:
            env["XV_TEXTURE_STATE_CACHE"] = configured
        subprocess.run([str(exe), "--settings"], env=env, check=True)
