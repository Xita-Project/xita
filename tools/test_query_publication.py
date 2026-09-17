#!/usr/bin/env python3
"""Real production prefix scan/retirement/completer with synthetic GPU readiness."""
import argparse, hashlib, json, os, pathlib, resource, subprocess, tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(out):
    sdk = pathlib.Path(os.environ.get("VITASDK", str(pathlib.Path.home() / "vitasdk")))
    main = (ROOT / "runtime/main.c").read_text()
    d3d = (ROOT / "runtime/xv_d3d.c").read_text()
    a = main.index("static struct {", main.index("static volatile int      g_running"))
    b = main.index("static xv_slot_owner", a)
    (out / "packets.inc").write_text(main[a:b])
    a = main.index("static unsigned g_retired_count")
    b = main.index("static int xv_pump_thread", a)
    (out / "retire.inc").write_text(main[a:b])
    a = main.index("            g_packets[q].visibility_fence=(SceGxmNotification){NULL,0};")
    b = main.index("#ifdef XV_QUERY_BOUNDARY", a)
    (out / "initialize_prefix.inc").write_text(main[a:b])
    a = d3d.index("void xv_d3d_visibility_complete(")
    b = d3d.index("static SceGxmBlendFactor alpha_blend_factor", a)
    (out / "complete.inc").write_text(d3d[a:b].replace(
        "void xv_d3d_visibility_complete(", "static void complete_actual("))
    rows = []
    for enabled in (0, 1):
        for timing in (0, 1):
            exe = out / f"publication-{enabled}-{timing}"
            cmd = ["cc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                   "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter",
                   "-Wno-address", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-no-pie", "-DXV_RUN_RECOMP", "-DXV_QUERY_BOUNDARY",
                   "-DXV_FLARE_QUERY_OVERLAP", f"-DXV_QUERY_PREFIX_PUBLISH={enabled}",
                   f"-DXV_GPU_PACKET_TIMING={timing}", "-I" + str(out),
                   "-I" + str(ROOT), "-I" + str(ROOT / "runtime"),
                   "-idirafter", str(sdk / "arm-vita-eabi/include"),
                   str(ROOT / "tools/tests/query_publication.c"), "-o", str(exe)]
            subprocess.run(cmd, check=True)
            result = subprocess.run([str(exe)], check=True, text=True,
                                    stdout=subprocess.PIPE,
                                    env={**os.environ, "ASAN_OPTIONS": "detect_leaks=1"})
            rows.append({"enabled": enabled, "packet_timing": timing,
                         "command": cmd, "output": result.stdout})
            print(result.stdout, end="")
    # Prove the fixture notices the ownership/order/history failures this change
    # is meant to prevent. Only private generated includes are mutated.
    negative = []
    header = (ROOT / "runtime/xv_query_publication.h").read_text()
    controls = {
        "ignore-history-collision": ("if(!xv_d3d_visibility_publication_safe(mesh,prior,prior_count))", "if((xv_d3d_visibility_publication_safe(mesh,prior,prior_count),0))"),
        "skip-older-incomplete": ("!=ticket)return;", "!=ticket)continue;"),
        "retire-on-prefix": ("g_packets[q].visibility_completed=1;", "g_packets[q].visibility_completed=1; g_frame_completed=ticket;"),
    }
    for name, (old, new) in controls.items():
        assert header.count(old) == 1
        folder = out / name
        folder.mkdir(exist_ok=True)
        for include in ("packets.inc", "complete.inc", "retire.inc", "initialize_prefix.inc"):
            (folder / include).write_bytes((out / include).read_bytes())
        (folder / "xv_query_publication.h").write_text(header.replace(old, new))
        command = rows[2]["command"].copy()  # ON, normal packet timing.
        command[command.index("-I" + str(out))] = "-I" + str(folder)
        command[-1] = str(folder / "test")
        subprocess.run(command, check=True)
        result = subprocess.run([command[-1]], text=True, capture_output=True,
                                preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        assert result.returncode != 0 and "Assertion" in result.stderr, result.stderr
        negative.append({"name": name, "returncode": result.returncode, "stderr": result.stderr})
        print("PASS negative control:", name)
    receipt = {"rows": rows, "negative_controls": negative, "production_sources": {
        str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in (ROOT / "runtime/main.c", ROOT / "runtime/xv_d3d.c",
                  ROOT / "runtime/xv_query_publication.h", ROOT / "runtime/xv_visibility.h")},
        "scope": "actual extracted production helper bodies; synthetic notification words"}
    (out / "host-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=pathlib.Path)
    args = parser.parse_args()
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
        run(args.out.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix="xita-query-publication-") as tmp:
            run(pathlib.Path(tmp))
