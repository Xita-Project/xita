#!/usr/bin/env python3
"""Compile and execute lifted synthetic LOOP branches; no commercial inputs."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as recomp
from tools.test_game_profiles import fixture


class LoopBranches(unittest.TestCase):
    def compile_and_run(self, opcode, mode):
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "a host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="xita-loop-test-") as directory:
            root = Path(directory)
            data, _ = fixture()
            data = bytearray(data)
            data[0x1000:] = bytes(len(data) - 0x1000)
            # Each path changes EAX, then returns. External branches target an
            # unmapped address and must dispatch without pushing another return.
            if mode == "external":
                program = bytes([opcode, 0xFC]) + b"\xB8\x11\0\0\0\xC3"
            elif mode == "forward":
                program = bytes([opcode, 6]) + b"\xB8\x11\0\0\0\xC3\xB8\x22\0\0\0\xC3"
            else:
                program = bytes([opcode, 0xFE]) + b"\xB8\x11\0\0\0\xC3"
            data[0x1000:0x1000 + len(program)] = program
            xbe = root / "test.xbe"
            xbe.write_bytes(data)
            image = recomp.Image(str(xbe))
            discovery = recomp.Discovery(image, {}, {}, lambda *_: None)
            discovery.add_root(image.entry)
            discovery.run()
            emitter = recomp.Emitter(image, discovery, {}, {}, str(root), 1)
            emitter.write_all()
            self.assertEqual(dict(emitter.unimpl), {})
            cases = []
            # Check both ZF values and the ECX 0 -> UINT32_MAX wrap. Backward
            # loops use finite counts; the other modes take at most one branch.
            for count in ([1, 2, 3] if mode == "backward" else [0, 1, 2]):
                for zero in [0, 1]:
                    enabled = opcode == 0xE2 or (zero == (opcode == 0xE1))
                    after = (count - 1) & 0xFFFFFFFF
                    taken = bool(after and enabled)
                    if mode == "backward" and taken:
                        after = 0
                    expected_calls = int(mode == "external" and taken)
                    expected_eax = 0x22 if taken and mode != "backward" else 0x11
                    cases.append(f"check({count}u, {zero}, {after}u, {expected_calls}, {expected_eax}u);")
            harness = root / "harness.c"
            harness.write_text('''
#include <assert.h>
#include "code_000.c"
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static unsigned calls;
void xv_call(xctx *c, uint32_t target) {
    assert(target == 0x10FFEu);
    assert(c->r[4] == 0x8000u);
    ++calls;
    c->r[0] = 0x22;
    c->r[4] += 4;
}
void xv_preempt(xctx *c) { c->preempt = 100; }
void xv_trap(xctx *c, uint32_t address) {
    (void)c; (void)address;
    assert(!"unexpected trap");
}
static void check(uint32_t count, int zero, uint32_t after,
                  unsigned expected_calls, uint32_t expected_eax) {
    xctx c = {0};
    c.r[1] = count;
    c.r[4] = 0x8000;
    c.f_kind = XK_EXPLICIT;
    c.f_res = 0x801u | ((uint32_t)zero << 6);
    uint32_t flags = c.f_res;
    calls = 0;
    f_00011000(&c);
    assert(c.r[1] == after);
    assert(c.r[0] == expected_eax);
    assert(c.r[4] == 0x8004u);
    assert(c.f_kind == XK_EXPLICIT && c.f_res == flags);
    assert(calls == expected_calls);
}
int main(void) {
''' + "\n".join(cases) + "\nreturn 0;\n}\n")
            executable = root / "harness"
            result = subprocess.run([compiler, "-std=gnu11", "-O2", "-I", str(ROOT / "recomp"),
                                     str(harness), "-o", str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_loop_targets_and_conditions(self):
        for opcode in [0xE0, 0xE1, 0xE2]:
            for mode in ["external", "forward", "backward"]:
                with self.subTest(opcode=hex(opcode), mode=mode):
                    self.compile_and_run(opcode, mode)


if __name__ == "__main__":
    unittest.main()
