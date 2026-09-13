#!/usr/bin/env python3
"""Exercise the production immediate-flare bridge with a recording sink."""
import os
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
sdk = pathlib.Path(os.environ.get("VITASDK", str(pathlib.Path.home() / "vitasdk")))
s = (root / "runtime/xv_ui_gxm.c").read_text()
parts = []
for begin, end in [("static inline uint32_t pack_argb(", "void xv_ui_gxm_clear("),
                   ("static unsigned g_flare_seen", "void xd3d_r_im_end(")]:
    a = s.index(begin); parts.append(s[a:s.index(end, a)])
with tempfile.TemporaryDirectory(prefix="xita-flare-test-") as d:
    d = pathlib.Path(d)
    (d / "flare_under_test.inc").write_text("\n".join(parts))
    exe = d / "test"
    subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
                    "-fsanitize=address,undefined", "-I", str(root), "-I", str(root / "runtime"), "-I", str(d),
                    "-idirafter", str(sdk / "arm-vita-eabi/include"),
                    str(root / "tools/tests/immediate_flare.c"), "-o", str(exe)], check=True)
    env = os.environ.copy()
    env.pop("XV_WCLAMP", None)
    env.pop("XITA_TEST_FLARE_CULL_DISABLED", None)
    env["XV_FLARE_CULL"] = "1"
    subprocess.run([str(exe)], check=True, env=env)
    env["XITA_TEST_FLARE_CULL_DISABLED"] = "1"
    env["XV_FLARE_CULL"] = "0"
    subprocess.run([str(exe)], check=True, env=env)
    env["XV_FLARE_CULL"] = "1"
    env["XV_WCLAMP"] = "1"
    subprocess.run([str(exe)], check=True, env=env)
