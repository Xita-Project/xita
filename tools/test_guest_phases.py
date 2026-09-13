#!/usr/bin/env python3
"""Selected-scope generation and real runtime accounting; synthetic inputs only."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.test_game_profiles import fixture
from recompiler.xita_recomp import Image, Discovery, Emitter
from recompiler.core.hooks import NoGameHooks
from tools.analyze_guest_phases import parse_windows, summarize

# Reject partial/corrupt capture windows; rank self time without adding nested
# inclusive costs. Open scopes can legitimately have zero entries this window.
header = "[guest-phase] 60 frames end-frame 120 window-us 600000 dropped 0 invalid 0; totals\n"
rows = ("[guest-phase] 00011000 root calls 0 active-us 60000 self-us 12000 parked-us 1000 parked-self-us 0\n"
        "[guest-phase] 00011020 child calls 60 active-us 48000 self-us 48000 parked-us 1000 parked-self-us 1000\n")
end = "[guest-phase] report-us 600\n"
capture = header + rows + "[fps] unrelated diagnostic\n" + end
windows, errors = parse_windows(capture)
assert not errors and len(windows) == 1 and windows[0]["usable"]
report = summarize(windows)
assert report["frames"] == 60 and report["report_ms_per_frame"] == .01
assert report["ranked_by_self"][0]["name"] == "child"
assert report["ranked_by_self"][0]["self_ms_per_frame"] == .8
assert report["ranked_by_self"][1]["calls"] == 0
for corrupt in [capture.replace("dropped 0", "dropped 1"),
                capture.replace("invalid 0", "invalid 1"),
                capture.replace("60 frames", "0 frames"),
                header + rows + rows + end,
                capture.replace("self-us 12000", "self-us 60001"),
                capture.replace("parked-self-us 1000", "parked-self-us 1001"),
                header + rows + "[guest-phase] truncated row\n" + end]:
    rejected, _ = parse_windows(corrupt)
    assert len(rejected) == 1 and not rejected[0]["usable"]
    assert summarize(rejected)["frames"] == 0
partial, errors = parse_windows(header + rows)
assert not partial and len(errors) == 1
recovered, errors = parse_windows(header + rows + capture)
assert len(recovered) == 1 and recovered[0]["usable"] and len(errors) == 1
assert summarize([])["ranked_by_self"] == []
print("PASS: capture truncation, corrupt windows, nested self ranking and recovery")

with tempfile.TemporaryDirectory(prefix="xita-guest-phases-") as tmp:
    p = Path(tmp)
    cc = os.environ.get("CC", "cc")
    flags = [cc, "-std=gnu11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
             "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"]
    subprocess.run([*flags, str(ROOT / "recomp/host/guest_phase_test.c"), "-o", str(p / "runtime")], check=True)
    subprocess.run([str(p / "runtime")], check=True)
    subprocess.run([*flags, str(ROOT / "recomp/host/guest_phase_batch_test.c"), "-o", str(p / "batch")], check=True)
    subprocess.run([str(p / "batch")], check=True)
    # Opt-in on a regular, uninstrumented build must stay disabled.
    (p / "absent.c").write_text('''#include "xv_phase.h"
#include <assert.h>
uint64_t xk_os_monotonic_us(void) { assert(0);return 0; }
void xk_os_log(const char *fmt,...) {(void)fmt;}
int main(void){xv_phase_init();assert(!xv_phase_enabled);xv_phase_frame(60);}
''')
    subprocess.run([*flags, "-I", str(ROOT / "recomp"), str(p / "absent.c"),
                    str(ROOT / "recomp/xv_phase.c"), "-o", str(p / "absent")], check=True)
    subprocess.run([str(p / "absent")], env={**os.environ, "XV_PHASE_TIMING": "1"}, check=True)

    data, _ = fixture()
    xbe = p / "test.xbe"
    xbe.write_bytes(data)
    img = Image(str(xbe))
    disc = Discovery(img, {}, {}, lambda *args: None)
    disc.add_root(img.entry)
    disc.run()
    em = Emitter(img, disc, {}, {}, str(p / "plain"), 1)
    before = {a: em.emit_function(fn) for a, fn in disc.functions.items()}
    em.write_all()
    assert "xv_phase" not in (p / "plain/xv_recomp_protos.h").read_text()
    assert "xv_phase" not in (p / "plain/xv_fn_table.c").read_text()
    em.phase_targets = {0x11000: "root"}
    em.outdir = str(p / "timed")
    em.write_all()
    for a, fn in disc.functions.items():
        actual = em.emit_function(fn)
        assert actual.replace("    XV_PHASE_SCOPE(c, 0u);\n", "") == before[a]
        assert actual.count("XV_PHASE_SCOPE") == (a == 0x11000)
    table = (p / "timed/xv_fn_table.c").read_text()
    assert "xv_phase_target_count = 1" in table and '{ 0x00011000u, "root" }' in table
    subprocess.run([cc, "-std=gnu11", "-Werror", "-Wno-unused-label", "-Wno-unused-variable",
                    "-I", str(ROOT / "recomp"), "-fsyntax-only", str(p / "timed/code_000.c")], check=True)
    for targets in [{0xDEAD: "missing"}, {0x11000: 'bad"name'}, {0x11000: "x" * 49}]:
        em.phase_targets = targets
        try:
            em.write_all()
            raise AssertionError("invalid phase target accepted")
        except ValueError:
            pass
    try:
        NoGameHooks().phase_targets()
        raise AssertionError("unreviewed adapter accepted")
    except ValueError:
        pass
    print("PASS: default generation unchanged, selected scopes only, target guards, absent metadata and generated C syntax")
