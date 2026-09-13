#!/usr/bin/env python3
"""Generated pointer accesses honor an opt-in diagnostic policy before memory I/O."""
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


class GuestAddressCheck(unittest.TestCase):
    def test_generated_and_split_page_accesses(self):
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix="xita-address-check-") as directory:
            root = Path(directory)
            data = bytearray(fixture()[0])
            cases = {
                0x11000: bytes.fromhex("8b01c3"),          # MOV EAX,[ECX]; RET
                0x11010: bytes.fromhex("8901c3"),          # MOV [ECX],EAX; RET
                0x11020: bytes.fromhex("a1041800fdc3"),    # MOV EAX,[FD001804]; RET
                0x11030: bytes.fromhex("0f1001c3"),        # MOVUPS XMM0,[ECX]; RET
            }
            for address, code in cases.items():
                offset = address - 0x11000 + 0x1000
                data[offset:offset + len(code)] = code
            xbe = root / "synthetic.xbe"
            xbe.write_bytes(data)
            image = recomp.Image(str(xbe))
            discovery = recomp.Discovery(image, {}, {}, lambda *_: None)
            for address in cases:
                discovery.add_root(address)
            discovery.run()
            emitter = recomp.Emitter(image, discovery, {}, {}, str(root), 1)
            emitter.write_all()
            self.assertEqual(dict(emitter.unimpl), {})
            harness = root / "harness.c"
            harness.write_text(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include "code_000.c"
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static jmp_buf escape;
static unsigned checked;
static uint32_t observed;
#ifdef XV_CHECK_GUEST_ADDRESS
void xv_check_guest_address(uint32_t address)
{
    ++checked;
    observed = address;
    if (address >= 0xFD000000u && address < 0xFE000000u) longjmp(escape, 1);
}
#endif
static void expect_blocked(void (*run)(xctx *), uint32_t address)
{
    xctx c = {0}; c.r[1] = address; c.r[4] = 0x1000; c.r[0] = 0xAABBCCDD;
    uint8_t before[0x5000]; memcpy(before, g_xram, sizeof before);
    checked = 0;
#ifdef XV_CHECK_GUEST_ADDRESS
    if (!setjmp(escape)) { run(&c); abort(); }
    assert(checked == 1 && observed == address);
    assert(memcmp(before, g_xram, sizeof before) == 0);
#else
    /* Default builds need no policy symbol and preserve their normal mapping. */
    run(&c); assert(!checked);
    memcpy(g_xram, before, sizeof before);
#endif
}
int main(void)
{
    g_xram = calloc(1, 0x5000); g_xpt = calloc(1u << 20, sizeof *g_xpt);
    assert(g_xram && g_xpt);
    for (unsigned i = 0; i < (1u << 20); ++i) g_xpt[i] = 0x4000;
    g_xpt[1] = 0; g_xpt[2] = 0x2000;
    xctx c = {0}; c.r[1] = 0x1104; c.r[4] = 0x1000;
    const uint32_t value = 0x12345678;
    memcpy(g_xram + 0x104, &value, sizeof value);
    f_00011000(&c); assert(c.r[0] == value && c.r[4] == 0x1004);
    c.r[0] = 0x87654321; c.r[4] = 0x1000;
    f_00011010(&c);
    uint32_t written; memcpy(&written, g_xram + 0x104, sizeof written);
    assert(written == c.r[0] && c.r[4] == 0x1004);
#ifdef XV_CHECK_GUEST_ADDRESS
    assert(checked == 2 && observed == 0x1104);
#else
    assert(!checked);
#endif
    expect_blocked(f_00011000, 0xFD001804);
    expect_blocked(f_00011010, 0xFD600140);
    expect_blocked(f_00011020, 0xFD001804);
    expect_blocked(f_00011030, 0xFD001800);
    /* Split copies must check both separately mapped guest pages. */
    uint8_t pattern[16], copy[16];
    for (unsigned i = 0; i < sizeof pattern; ++i) pattern[i] = 0x40 + i;
    checked = 0;
    x_guest_write(0x1FF8, pattern, sizeof pattern);
    x_guest_read(copy, 0x1FF8, sizeof copy);
    assert(memcmp(pattern, copy, sizeof copy) == 0);
    assert(memcmp(g_xram + 0xFF8, pattern, 8) == 0);
    assert(memcmp(g_xram + 0x2000, pattern + 8, 8) == 0);
#ifdef XV_CHECK_GUEST_ADDRESS
    assert(checked == 4 && observed == 0x2000);
    checked = 0;
    if (!setjmp(escape)) { x_guest_read(copy, 0xFCFFFFF8, sizeof copy); abort(); }
    assert(checked == 2 && observed == 0xFD000000);
#endif
    free(g_xpt); free(g_xram);
    return 0;
}
''')
            for flags in [[], ["-DXV_CHECK_GUEST_ADDRESS=1"]]:
                with self.subTest(flags=flags):
                    executable = root / "check"
                    result = subprocess.run([compiler, "-std=gnu11", "-O2", "-ffunction-sections",
                                             "-fdata-sections", "-fno-strict-aliasing", *flags,
                                             "-I", str(ROOT / "recomp"), str(harness),
                                             str(ROOT / "recomp/xv_x86rt.c"), "-Wl,--gc-sections",
                                             "-lm", "-o", str(executable)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
