"""Execute synthetic x86 bit strings, including signed offsets and split pages."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as recomp
from tools.test_game_profiles import fixture


class BitStrings(unittest.TestCase):
    def test_memory_offsets_and_register_modulo(self):
        with tempfile.TemporaryDirectory(prefix="xita-bit-string-") as directory:
            root = Path(directory)
            original, _ = fixture()
            data = bytearray(0x2000)
            data[:len(original)] = original
            data[0x1000:] = b"\xcc" * 0x1000
            struct.pack_into("<I", data, 0x408, 0x1000)
            struct.pack_into("<I", data, 0x410, 0x1000)
            cases = []
            for size in (2, 4):
                for op, opcode in enumerate((0xA3, 0xAB, 0xB3, 0xBB)):
                    # [ECX], EAX; [EAX], EAX; [ECX], imm255;
                    # EAX, ECX; EAX, EAX. 66 selects AX/CX and word memory.
                    for form in range(5):
                        code = bytes([0x66]) if size == 2 else b""
                        code += (bytes([0x0F, 0xBA, 0x21 + op * 8, 255]) if form == 2
                                 else bytes([0x0F, opcode, (0x01, 0x00, 0, 0xC8, 0xC0)[form]]))
                        code += b"\xc3"
                        address = 0x11000 + len(cases) * 16
                        data[address - 0x10000:address - 0x10000 + len(code)] = code
                        cases.append((address, size, op, form))
            xbe = root / "synthetic.xbe"
            xbe.write_bytes(data)
            image = recomp.Image(str(xbe))
            discovery = recomp.Discovery(image, {}, {}, lambda *_: None)
            for address, *_ in cases:
                discovery.add_root(address)
            discovery.run()
            emitter = recomp.Emitter(image, discovery, {}, {}, str(root), 1)
            emitter.write_all()
            self.assertEqual(dict(emitter.unimpl), {})
            entries = "\n".join(f"{{f_{a:08X}, {s}, {o}, {f}}}," for a, s, o, f in cases)
            harness = root / "harness.c"
            harness.write_text(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "code_000.c"
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
typedef struct { void (*run)(xctx *); unsigned size, op, form; } test_case;
static const test_case cases[] = {
''' + entries + r'''
};
static int64_t floor_div(int64_t n, unsigned d)
{ return n >= 0 ? n / d : -((-n + d - 1) / d); }
static const uint32_t indices[] = {
    0, 1, 15, 16, 31, 32, 33, 45, 105, 255, 256,
    -1u, -16u, -17u, -31u, -32u, -33u, -65u,
    0x8000, 0x7FFF, 0x80000000, 0x7FFFFFFF
};
int main(void)
{
    g_xram = malloc(0x10000); g_img_base = g_xram;
    g_xpt = calloc(1u << 20, 4); assert(g_xram && g_xpt);
    uint8_t expected[0x10000];
    for (unsigned n = 0; n < sizeof cases / sizeof cases[0]; ++n)
    for (unsigned k = 0; k < sizeof indices / sizeof indices[0]; ++k)
    for (unsigned boundary = 0; boundary < 2; ++boundary) {
        const test_case *t = &cases[n];
        unsigned bits = t->size * 8;
        for (unsigned p = 0; p < (1u << 20); ++p) g_xpt[p] = 0xF000;
        for (unsigned i = 0; i < 0x10000; ++i) g_xram[i] = (uint8_t)(i * 19 + k * 37);
        xctx c = {0}; c.r[4] = 0x8000; c.r[0] = indices[k];
        c.r[1] = 0x10001000u - boundary; c.fs_base = 0x12345678;
        c.f_kind = XK_EXPLICIT; c.f_res = 0x8D5;
        c.r[2] = 0xFEEDFACE; c.xmm[0][0] = -3.5f;
        if (t->form >= 3) { c.r[1] = indices[k]; c.r[0] = 0xABCD4321 + k; }
        uint32_t raw_index = t->form == 2 ? 255 : t->form == 3 ? c.r[1] : c.r[0];
        int64_t index = bits == 16 ? (int16_t)raw_index : (int32_t)raw_index;
        uint32_t base = t->form == 1 ? c.r[0] : c.r[1];
        if (t->form >= 2) index = raw_index & (bits - 1);
        uint32_t byte_address = base + (uint32_t)floor_div(index, 8);
        unsigned bit = (unsigned)(index - floor_div(index, 8) * 8);
        uint32_t word_address = base + (uint32_t)(floor_div(index, bits) * t->size);
        g_xpt[word_address >> 12] = 0x5000;
        g_xpt[(word_address + t->size - 1) >> 12] =
            (word_address >> 12) == ((word_address + t->size - 1) >> 12) ? 0x5000 : 0x9000;
        memcpy(expected, g_xram, sizeof expected);
        xctx want = c; want.r[4] += 4; want.f_cf_override = 1;
        if (t->form < 3) {
            unsigned host = g_xpt[byte_address >> 12] + (byte_address & 4095);
            want.f_cf = (expected[host] >> bit) & 1;
            if (t->op == 1) expected[host] |= 1u << bit;
            if (t->op == 2) expected[host] &= ~(1u << bit);
            if (t->op == 3) expected[host] ^= 1u << bit;
        } else {
            bit = raw_index & (bits - 1);
            want.f_cf = (c.r[0] >> bit) & 1;
            if (t->op == 1) want.r[0] |= 1u << bit;
            if (t->op == 2) want.r[0] &= ~(1u << bit);
            if (t->op == 3) want.r[0] ^= 1u << bit;
        }
        t->run(&c);
        if (memcmp(&c, &want, sizeof c) || memcmp(g_xram, expected, sizeof expected)) {
            fprintf(stderr, "case %u index %08X boundary %u\n", n, indices[k], boundary); abort();
        }
    }
    free(g_xpt); free(g_xram);
    return 0;
}
''')
            for flags in (("-O2",), ("-O1", "-fsanitize=address,undefined", "-fno-omit-frame-pointer")):
                executable = root / "harness"
                subprocess.run(["cc", "-std=gnu11", *flags, "-fno-strict-aliasing",
                                "-ffunction-sections", "-fdata-sections", "-I", str(ROOT / "recomp"),
                                str(harness), str(ROOT / "recomp/xv_x86rt.c"),
                                "-Wl,--gc-sections", "-lm", "-o", str(executable)], check=True,
                               capture_output=True, text=True)
                result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
