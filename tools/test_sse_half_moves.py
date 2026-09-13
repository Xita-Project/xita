#!/usr/bin/env python3
"""Bit-exact synthetic MOVHLPS/MOVLHPS execution across all XMM register pairs."""
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as recomp
from tools.test_game_profiles import fixture


class SseHalfMoves(unittest.TestCase):
    def test_lane_bits_and_register_aliasing(self):
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "a host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="xita-half-moves-") as directory:
            root = Path(directory)
            original, _ = fixture()
            data = bytearray(0x2000)
            data[:len(original)] = original
            data[0x1000:] = b"\xCC" * 0x1000
            struct.pack_into("<I", data, 0x408, 0x1000)  # section VirtualSize
            struct.pack_into("<I", data, 0x410, 0x1000)  # section RawSize
            cases = []
            for opcode in [0x12, 0x16]:
                for destination in range(8):
                    for source in range(8):
                        offset = len(cases) * 16
                        data[0x1000 + offset:0x1004 + offset] = bytes(
                            [0x0F, opcode, 0xC0 | (destination << 3) | source, 0xC3])
                        cases.append((0x11000 + offset, destination, source, int(opcode == 0x12)))
            xbe = root / "test.xbe"
            xbe.write_bytes(data)
            image = recomp.Image(str(xbe))
            discovery = recomp.Discovery(image, {}, {}, lambda *_: None)
            for address, *_ in cases:
                discovery.add_root(address)
            discovery.run()
            emitter = recomp.Emitter(image, discovery, {}, {}, str(root), 1)
            emitter.write_all()
            self.assertEqual(len(discovery.functions), 128)
            self.assertEqual(dict(emitter.unimpl), {})
            entries = "\n".join(f"{{f_{address:08X}, {dst}, {src}, {hl}}},"
                                for address, dst, src, hl in cases)
            harness = root / "harness.c"
            harness.write_text('''
#include <assert.h>
#include "code_000.c"
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
typedef struct { void (*run)(xctx *); unsigned dst, src, high_to_low; } test_case;
static const test_case cases[] = {
''' + entries + '''
};
/* Zeros, infinities, signaling/quiet NaN payloads, subnormals and normal values.
 * Use integer snapshots: these instructions move bits without FP arithmetic.
 * Intel SDM Vol. 2B, MOVHLPS/MOVLHPS, legacy two-argument Operation sections.
 */
static const uint32_t patterns[] = {
    0x00000000u, 0x80000000u, 0x7F800000u, 0xFF800000u,
    0x7F800001u, 0x7FC12345u, 0xFF801234u, 0xFFC54321u,
    0x00000001u, 0x007FFFFFu, 0x80000001u, 0x807FFFFFu,
    0x00800000u, 0x80800000u, 0x3F800000u, 0xBF800000u,
    0x41234567u, 0xC1234567u, 0x01234567u
};
int main(void) {
    for (unsigned n = 0; n < sizeof(cases) / sizeof(cases[0]); ++n) {
        for (unsigned seed = 0; seed < sizeof(patterns) / sizeof(patterns[0]); ++seed) {
            const test_case *test = &cases[n];
            uint32_t before[8][4], wanted[8][4];
            for (unsigned reg = 0; reg < 8; ++reg)
                for (unsigned lane = 0; lane < 4; ++lane)
                    before[reg][lane] = patterns[(reg * 4 + lane + seed) % 19];
            memcpy(wanted, before, sizeof wanted);
            if (test->high_to_low) {
                wanted[test->dst][0] = before[test->src][2];
                wanted[test->dst][1] = before[test->src][3];
            } else {
                wanted[test->dst][2] = before[test->src][0];
                wanted[test->dst][3] = before[test->src][1];
            }
            xctx context = {0}, expected;
            context.r[4] = 0x8000;
            context.f_kind = XK_EXPLICIT;
            context.f_res = 0x8D5;
            memcpy(context.xmm, before, sizeof before);
            memcpy(&expected, &context, sizeof context);
            memcpy(expected.xmm, wanted, sizeof wanted);
            expected.r[4] += 4;  /* RET after the tested instruction */
            test->run(&context);
            assert(memcmp(&context, &expected, sizeof context) == 0);
        }
    }
    return 0;
}
''')
            for flags in [["-O2"], ["-O3", "-ffast-math", "-fstrict-aliasing"]]:
                with self.subTest(flags=flags):
                    executable = root / "harness"
                    result = subprocess.run([compiler, "-std=gnu11", *flags, "-I", str(ROOT / "recomp"),
                                             str(harness), "-o", str(executable)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
