#!/usr/bin/env python3
"""Synthetic x86 execution through the explicit Halo 2 graphics bus lowering."""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from games import load_hooks
from games.halo2_5849.hooks import lower_bus_mov, lower_bus_compare
from recompiler.core.hooks import NoGameHooks
from recompiler import xita_recomp as recomp
from tools.test_game_profiles import fixture


class SyntheticBusHooks(NoGameHooks):
    def lower_instruction(self, emitter, instruction, output):
        return lower_bus_mov(emitter, instruction, output) or lower_bus_compare(emitter, instruction, output)


class Halo2Bus(unittest.TestCase):
    def test_generated_bus_and_cpu_effects(self):
        with tempfile.TemporaryDirectory(prefix="xita-h2-bus-") as directory:
            root = Path(directory)
            data = bytearray(fixture()[0])
            data.extend(bytes(0x1200 - len(data)))
            struct.pack_into("<I", data, 0x408, 0x200)
            struct.pack_into("<I", data, 0x410, 0x200)
            programs = {
                # Original synthetic driver: enable bus master and inspect identity/RAM.
                0x11000: "89c38b830418000083c8048983041800008b8b001800008b930c0210009c5dc3",
                0x11040: "8b01c3",  # EAX <- [ECX]
                0x11050: "8911c3",  # [ECX] <- EDX
                0x11060: "a1041800fdc3",  # absolute PCI command read
                0x11070: "8b09c3",  # address register aliases destination
                0x11090: "0fb609c3",  # MOVZX ECX, byte [ECX], address/destination alias
                0x110A0: "0fb701c3",  # MOVZX EAX, word [ECX]
                0x110B0: "8a21c3",  # MOV AH,[ECX]
                0x110C0: "8a09c3",  # MOV CL,[ECX], partial address alias
                0x110D0: "668b01c3",  # MOV AX,[ECX]
                0x110E0: "8821c3",  # MOV [ECX],AH
                0x110F0: "668911c3",  # MOV [ECX],DX
                0x11100: "c6011fc3",  # byte immediate
                0x11110: "8c19c3",  # MOV [ECX],DS must retain existing segment path
                0x11120: "3b019c5dc3",  # CMP EAX,[ECX]
                0x11130: "39019c5dc3",  # CMP [ECX],EAX
                0x11140: "663b019c5dc3",  # CMP AX,[ECX]
                0x11150: "6639019c5dc3",  # CMP [ECX],AX
                0x11160: "3a219c5dc3",  # CMP AH,[ECX]
                0x11170: "38219c5dc3",  # CMP [ECX],AH
                0x11180: "8339ff9c5dc3",  # CMP dword [ECX], sign-extended -1
                0x11190: "668339ff9c5dc3",  # CMP word [ECX], sign-extended -1
                0x111A0: "8039ff9c5dc3",  # CMP byte [ECX], FF
                0x11080: "894908c3",  # store address register as data
            }
            for address, code in programs.items():
                blob = bytes.fromhex(code)
                offset = address - 0x10000
                data[offset:offset + len(blob)] = blob
            xbe = root / "synthetic.xbe"
            xbe.write_bytes(data)
            image = recomp.Image(str(xbe))
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                load_hooks("halo2_5849_graphics", image)
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                load_hooks("halo2_5849_host_channel", image)
            discovery = recomp.Discovery(image, {}, {}, lambda *_: None)
            for address in programs:
                discovery.add_root(address)
            discovery.run()
            plain = recomp.Emitter(image, discovery, {}, {}, str(root), 1)
            self.assertNotIn("h2_bus", plain.emit_function(discovery.functions[0x11000]))
            emitter = recomp.Emitter(image, discovery, {}, {}, str(root), 1, SyntheticBusHooks())
            emitter.write_all()
            self.assertNotIn("h2_bus", emitter.emit_function(discovery.functions[0x11110]))
            self.assertEqual(dict(emitter.unimpl), {})
            harness = root / "harness.c"
            harness.write_text(r'''
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <stdio.h>
#include "code_000.c"
#include "gpu_bus.h"
#include "nv2a_regs.h"
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static jmp_buf escape;
static uint64_t now_us;
uint32_t h2_instance_bytes(void) { return 0x5000; }
uint64_t h2_graphics_time_us(void) { return now_us; }
static uint32_t stopped_ip, stopped_address;
static int stopped_write, stopped_reason;
void xv_logf(const char *format, ...) { (void)format; }
void xv_check_guest_address(uint32_t address)
{ assert(address < 0xFD000000u || address >= 0xFE000000u); }
void h2_graphics_stop(xctx *c, uint32_t ip, uint32_t address, uint32_t value, int write, int reason)
{
    (void)c; (void)value;
    stopped_ip = ip; stopped_address = address; stopped_write = write; stopped_reason = reason;
    longjmp(escape, 1);
}
static uint32_t comparison_flags(uint32_t a, uint32_t b, unsigned bits)
{
    uint32_t mask = bits == 32 ? UINT32_MAX : (1u << bits) - 1;
    a &= mask; b &= mask;
    uint32_t result = (a - b) & mask, sign = 1u << (bits - 1);
    unsigned parity = 0;
    for (unsigned i = 0; i < 8; ++i) parity ^= (result >> i) & 1;
    /* Shared xf_eflags currently omits AF; preserve that existing contract. */
    return (a < b) | ((!parity) << 2) |
           ((!result) << 6) | (!!(result & sign) << 7) |
           (!!((a ^ b) & (a ^ result) & sign) << 11);
}
int main(void)
{
    g_xram = calloc(1, 0xB000); g_xpt = calloc(1u << 20, 4); assert(g_xram && g_xpt);
    for (unsigned i = 0; i < 5; ++i) g_xpt[0x83FEB + i] = 0x5000 + i * 4096;
    g_xpt[1] = 0; g_xpt[2] = 0x2000;
    h2_gpu_bus_reset(0x4000000);
    xctx c = {0}; c.r[0] = 0xFD000000; c.r[4] = 0x1800;
    f_00011000(&c);
    assert(c.r[0] == 6 && c.r[1] == 0x02A010DE && c.r[2] == 0x4000000);
    assert(c.r[3] == 0xFD000000 && c.r[4] == 0x1804);
    assert((c.r[5] & 0x8D5) == 4); /* OR result 6: parity, no CF/ZF/SF/OF */
    f_00011060(&c); assert(c.r[0] == 6);
    c.r[1] = 0xFD001800; f_00011070(&c); assert(c.r[1] == 0x02A010DE);
    /* Ordinary accesses preserve bits, including nonadjacent guest pages. */
    c.r[1] = 0x1FFE; c.r[2] = 0xF1234567;
    f_00011050(&c); f_00011040(&c); assert(c.r[0] == c.r[2]);
    assert(g_xram[0xFFE] == 0x67 && g_xram[0xFFF] == 0x45);
    assert(g_xram[0x2000] == 0x23 && g_xram[0x2001] == 0xF1);
    c.r[1] = 0x1100; f_00011080(&c);
    c.r[1] += 8; f_00011040(&c); assert(c.r[0] == 0x1100);
    /* Zero extension extracts little-endian byte lanes without changing flags. */
    uint32_t flags = xf_eflags(&c);
    c.r[1] = 0xFD680509; f_00011090(&c); assert(c.r[1] == 0xC2);
    assert(xf_eflags(&c) == flags);
    c.r[1] = 0xFD680508; f_000110A0(&c); assert(c.r[0] == 0xC20D);
    c.r[1] = 0x1FFF; f_000110A0(&c); assert(c.r[0] == 0x2345);
    c.r[1] = 0x2001; f_00011090(&c); assert(c.r[1] == 0xF1);
    assert(xf_eflags(&c) == flags);
    /* Partial loads preserve the untouched lanes and use the old address. */
    c.r[0] = 0x12345678; c.r[1] = 0xFD680509;
    f_000110B0(&c); assert(c.r[0] == 0x1234C278);
    f_000110C0(&c); assert(c.r[1] == 0xFD6805C2);
    c.r[1] = 0xFD680508; f_000110D0(&c); assert(c.r[0] == 0x1234C20D);
    c.r[1] = 0x1FFF; c.r[2] = 0xDEAD8765; f_000110F0(&c);
    assert(g_xram[0xFFF] == 0x65 && g_xram[0x2000] == 0x87);
    assert(g_xram[0xFFE] == 0x67 && g_xram[0x2001] == 0xF1);
    f_000110D0(&c); assert(c.r[0] == 0x12348765);
    c.r[1] = 0x2001; f_000110E0(&c); assert(g_xram[0x2001] == 0x87);
    assert(xf_eflags(&c) == flags);
    /* The native constructor's indexed unlock / latency transaction. */
    c.r[1] = 0xFD6013D4; f_00011100(&c);
    c.r[1] = 0xFD6013D5; f_000110B0(&c); assert((c.r[0] & 0xFF00) == 0);
    c.r[0] = 0x5700; f_000110E0(&c);
    f_000110B0(&c); assert(c.r[0] == 0x300);
    c.r[1] = 0xFD6013D4; c.r[0] = 0x5200; f_000110E0(&c);
    c.r[1] = 0xFD6013D5; f_000110B0(&c); assert(c.r[0] == 0);
    c.r[0] = 0x400; f_000110E0(&c); f_000110B0(&c); assert(c.r[0] == 0x400);
    assert(xf_eflags(&c) == flags);
    now_us = 1000000;
    c.r[1] = 0xFD009410; f_00011040(&c); assert(c.r[0] == 1);
    c.r[1] = 0xFD009400; f_00011040(&c); assert(c.r[0] == 0xBD0C4980);
    /* PRAMIN writes reach the claimed physical alias, with reversed 64-byte groups. */
    c.r[1] = 0xFD710000; c.r[2] = 0xD15EA5ED; f_00011050(&c);
    assert(X_M32(0x83FEFFC0) == 0xD15EA5ED);
    X_M32(0x83FEFFBC) = 0xC001CAFE;
    c.r[1] = 0xFD71007C; f_00011040(&c); assert(c.r[0] == 0xC001CAFE);
    c.r[1] = 0xFD71007E; f_00011090(&c); assert(c.r[1] == 1);
    c.r[1] = 0xFD715000;
    if (!setjmp(escape)) { f_00011040(&c); abort(); }
    assert(stopped_reason == H2_NV2A_UNSUPPORTED_OPERATION);
    /* CMP reads leave all data registers and RAM intact; flags cover signed
     * overflow, borrow, zero, sign and parity at each width. The inherited AF
     * omission in xf_eflags is outside this bus-read change. */
    void (*compare[3][3])(xctx *) = {
        {f_00011160, f_00011170, f_000111A0},
        {f_00011140, f_00011150, f_00011190},
        {f_00011120, f_00011130, f_00011180}
    };
    uint32_t samples[] = {0, 1, 15, 16, 127, 128, 255, 256, 32767, 32768, 65535, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    for (unsigned w = 0; w < 3; ++w) for (unsigned order = 0; order < 3; ++order)
    for (unsigned i = 0; i < sizeof samples / sizeof *samples; ++i)
    for (unsigned j = 0; j < sizeof samples / sizeof *samples; ++j) {
        unsigned bits = 8u << w;
        uint32_t a = samples[i], b = samples[j];
        c.r[1] = 0x1FFF; c.r[2] = b; f_00011050(&c);
        c.r[0] = w ? a : ((a & 255) << 8) | 0xABC00003;
        uint32_t eax = c.r[0], esp = c.r[4];
        compare[w][order](&c);
        uint32_t left = order ? b : a, right = order == 2 ? UINT32_MAX : order ? a : b;
        if ((c.r[5] & 0x8D5) != comparison_flags(left, right, bits)) fprintf(stderr, "CMP bits=%u order=%u a=%08X b=%08X flags=%X expect=%X\n", bits, order, a, b, c.r[5], comparison_flags(left, right, bits));
        assert((c.r[5] & 0x8D5) == comparison_flags(left, right, bits));
        assert(c.r[0] == eax && c.r[1] == 0x1FFF && c.r[2] == b && c.r[4] == esp + 4);
        f_00011040(&c); assert(c.r[0] == b);
        c.r[4] = 0x1800;
    }
    c.r[0] = 6; c.r[1] = 0xFD001804;
    f_00011120(&c); assert((c.r[5] & 0x8D5) == 0x44 && c.r[0] == 6);
    c.r[1] = 0xFD000100; uint32_t before_flags = xf_eflags(&c);
    if (!setjmp(escape)) { f_00011120(&c); abort(); }
    assert(stopped_ip == 0x11120 && stopped_address == c.r[1] && !stopped_write);
    assert(xf_eflags(&c) == before_flags);
    /* Unsupported reads/writes report the actual instruction and do not run on. */
    c.r[1] = 0xFD600140; c.r[2] = 1;
    if (!setjmp(escape)) { f_00011050(&c); abort(); }
    assert(stopped_ip == 0x11050 && stopped_address == 0xFD600140);
    assert(stopped_write && stopped_reason == H2_NV2A_UNSUPPORTED_OPERATION);
    f_00011040(&c); assert(c.r[0] == 0); /* rejected enable left register unchanged */
    c.r[1] = 0xFD000100;
    if (!setjmp(escape)) { f_00011040(&c); abort(); }
    assert(stopped_ip == 0x11040 && stopped_address == c.r[1] && !stopped_write);
    assert(stopped_reason == H2_NV2A_UNKNOWN_REGISTER);
    c.r[1] = 0xFCFFFFFE;
    if (!setjmp(escape)) { f_00011040(&c); abort(); }
    assert(stopped_reason == H2_NV2A_INVALID_ACCESS);
    free(g_xpt); free(g_xram);
    return 0;
}
''')
            executable = root / "bus"
            command = ["cc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Wno-unused-label",
                       "-Wno-clobbered", "-fno-strict-aliasing", "-DXV_CHECK_GUEST_ADDRESS=1",
                       "-ffunction-sections", "-fdata-sections", "-I", str(ROOT / "recomp"),
                       "-I", str(ROOT / "games/halo2_5849"), str(harness),
                       str(ROOT / "recomp/xv_x86rt.c"), str(ROOT / "games/halo2_5849/gpu_bus.c"),
                       str(ROOT / "games/halo2_5849/nv2a_regs.c"), "-Wl,--gc-sections", "-lm", "-o", str(executable)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
