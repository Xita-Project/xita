"""Execute synthetic sparse JMP tables; no commercial code or data."""
from pathlib import Path
import hashlib
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

from games.halo2_5849 import hooks
from recompiler import xita_recomp as recomp
from recompiler.core.hooks import NoGameHooks
from tools.test_game_profiles import fixture

ROOT = Path(__file__).resolve().parents[1]


class SparseHooks(NoGameHooks):
    def lower_instruction(self, emitter, instruction, output):
        return hooks.lower_sparse_jump(emitter, instruction, output)


def synthetic_image(path, index=0):
    data = bytearray(fixture()[0]); data.extend(bytes(0x1200 - len(data)))
    struct.pack_into("<I", data, 0x408, 0x200)
    struct.pack_into("<I", data, 0x410, 0x200)
    data[0x1000:] = bytes(len(data) - 0x1000)
    data[0x1000:0x1007] = bytes((0xFF, 0x24, 0x85 | (index << 3))) + struct.pack("<I", 0x11080)
    for offset, value in ((0x1010, 0xA1), (0x1020, 0xB2), (0x1030, 0xC3)):
        data[offset:offset + 6] = b"\xBA" + struct.pack("<I", value) + b"\xC3"
    struct.pack_into("<7I", data, 0x107C, 0x11030, 0x11010, 0, 0x11020, 0, 0x11020, 0x11030)
    path.write_bytes(data)
    return recomp.Image(str(path))


class SparseJump(unittest.TestCase):
    def test_widget_kind_revision_roots_and_shape(self):
        self.check_widget_roots("WIDGET_KIND_JUMP", hooks.reviewed_widget_kind_roots)

    def test_widget_field_revision_roots_and_shape(self):
        self.check_widget_roots("WIDGET_FIELD_JUMP", hooks.reviewed_widget_field_roots)

    def check_widget_roots(self, prefix, roots):
        with tempfile.TemporaryDirectory() as directory:
            image = synthetic_image(Path(directory) / "synthetic.xbe")
            changed = bytearray(image.data)
            struct.pack_into("<4I", changed, 0x1098, 0x11010, 0, 0x11030, 0xFFFFFFFF)
            image.data = bytes(changed)
            guards = tuple((a, n, hashlib.sha256(image.bytes_at(a, n)).hexdigest())
                           for a, n in ((0x11000, 7), (0x11080, 36)))
            with patch.object(hooks, prefix + "_TABLE", 0x11080), patch.object(hooks, prefix + "_GUARDS", guards):
                self.assertEqual(roots(image), {0x11010, 0x11020, 0x11030})
                with patch.object(image, "is_code", return_value=False):
                    with self.assertRaisesRegex(ValueError, "not executable"):
                        roots(image)
                changed[0x10A0] ^= 1; image.data = bytes(changed)
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    roots(image)
            with patch.object(hooks, prefix + "_IP", 0x11000), patch.object(hooks, prefix + "_TABLE", 0x11080):
                wrong = recomp.Decoder(32, bytes.fromhex("ff248580100100" if prefix == "WIDGET_FIELD_JUMP" else "ff248d80100100"), ip=0x11000).decode()
                with self.assertRaisesRegex(ValueError, "shape mismatch"):
                    hooks.lower_sparse_jump(None, wrong, [])

    def test_revision_targets_and_instruction_shape(self):
        with tempfile.TemporaryDirectory() as directory:
            image = synthetic_image(Path(directory) / "synthetic.xbe")
            guards = tuple((a, n, hashlib.sha256(image.bytes_at(a, n)).hexdigest())
                           for a, n in ((0x11000, 7), (0x11080, 24)))
            with patch.object(hooks, "SPARSE_JUMP_TABLE", 0x11080), patch.object(hooks, "SPARSE_JUMP_GUARDS", guards):
                self.assertEqual(hooks.reviewed_sparse_jump_roots(image), {0x11010, 0x11020, 0x11030})
                with patch.object(image, "is_code", return_value=False):
                    with self.assertRaisesRegex(ValueError, "not executable"):
                        hooks.reviewed_sparse_jump_roots(image)
                changed = bytearray(image.data); changed[0x1094] ^= 1; image.data = bytes(changed)
                with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                    hooks.reviewed_sparse_jump_roots(image)
            with patch.object(hooks, "SPARSE_JUMP_IP", 0x11000), patch.object(hooks, "SPARSE_JUMP_TABLE", 0x11080):
                wrong = recomp.Decoder(32, bytes.fromhex("ff248d80100100"), ip=0x11000).decode()
                with self.assertRaisesRegex(ValueError, "shape mismatch"):
                    hooks.lower_sparse_jump(None, wrong, [])
                wrong.ip = 0x12000
                self.assertFalse(hooks.lower_sparse_jump(None, wrong, []))

    def test_tail_dispatch_and_unchanged_default(self):
        self.check_tail_dispatch("SPARSE_JUMP")

    def test_widget_kind_tail_dispatch_and_unchanged_default(self):
        self.check_tail_dispatch("WIDGET_KIND_JUMP")

    def test_widget_field_tail_dispatch_and_unchanged_default(self):
        self.check_tail_dispatch("WIDGET_FIELD_JUMP")

    def check_tail_dispatch(self, prefix):
        index_register = 1 if prefix == "WIDGET_FIELD_JUMP" else 0
        compiler = shutil.which("cc"); self.assertIsNotNone(compiler)
        for enabled in (False, True):
            with self.subTest(hook=enabled), tempfile.TemporaryDirectory(prefix="xita-sparse-") as directory:
                root = Path(directory); image = synthetic_image(root / "synthetic.xbe", index_register)
                discovery = recomp.Discovery(image, {}, {}, lambda *_: None)
                for address in (0x11000, 0x11010, 0x11020, 0x11030): discovery.add_root(address)
                discovery.run()
                self.assertEqual(discovery.functions[0x11000].switch_tables[0x11000], [(0, 0x11010), (-1, 0x11030)])
                with patch.object(hooks, prefix + "_IP", 0x11000), patch.object(hooks, prefix + "_TABLE", 0x11080):
                    emitter = recomp.Emitter(image, discovery, {}, {}, str(root), 1,
                                             hooks=SparseHooks() if enabled else NoGameHooks())
                    emitter.write_all()
                self.assertFalse(emitter.unimpl)
                body = (root / "code_000.c").read_text()
                self.assertEqual(f"switch (c->r[{index_register}])" in body, not enabled)
                harness = root / "harness.c"
                harness.write_text('''
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include "code_000.c"
#undef X_G
#define X_G(a) ((void *)(g_xram + g_xpt[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 4095u)))
uint8_t *g_xram, *g_img_base;
uint32_t *g_xpt;
static jmp_buf fault;
static xctx cpu;
static unsigned calls;
void xv_preempt(xctx *c) { (void)c; }
void xv_trap(xctx *c, uint32_t address) { (void)c; (void)address; longjmp(fault, 1); }
void xv_call(xctx *c, uint32_t target) {
    assert(c->r[4] == 0x8000); ++calls;
    switch (target) {
    case 0x11010: f_00011010(c); return;
    case 0x11020: f_00011020(c); return;
    case 0x11030: f_00011030(c); return;
    default: assert(!target); xv_trap(c, target); return;
    }
}
static void check(uint32_t index, int should_trap, uint32_t marker, unsigned expected_calls) {
    memset(&cpu, 0xA6, sizeof cpu); cpu.r[0] = index; cpu.r[4] = 0x8000;
    xctx expected = cpu; calls = 0;
    if (!should_trap) { expected.r[2] = marker; expected.r[4] += 4; }
    int trapped = setjmp(fault);
    if (!trapped) f_00011000(&cpu);
    assert(!!trapped == should_trap && calls == expected_calls);
    assert(!memcmp(&cpu, &expected, sizeof cpu));
    assert(X_M32(0x8000) == 0x11223344); /* original return remains untouched */
}
int main(void) {
    g_xram = calloc(1, 0x20000); g_img_base = g_xram;
    g_xpt = calloc(1u << 20, sizeof *g_xpt); assert(g_xram && g_xpt);
    for (unsigned i = 0; i < 32; ++i) g_xpt[i] = i * 4096;
    uint32_t table[] = {0x11030, 0x11010, 0, 0x11020, 0, 0x11020, 0x11030, 0};
    memcpy(g_xram + 0x1107C, table, sizeof table); X_M32(0x8000) = 0x11223344;
''' + ('''
    check(0,0,0xA1,1); check(1,1,0,1); check(2,0,0xB2,1); check(3,1,0,1);
    check(4,0,0xB2,1); check(5,0,0xC3,1); check(6,1,0,1); check(UINT32_MAX,0,0xC3,1);
    X_M32(0x11080) = 0x11030; check(0,0,0xC3,1); /* actual table read */
''' if enabled else '''
    check(0,0,0xA1,0); check(UINT32_MAX,0,0xC3,0);
    for (unsigned i = 1; i <= 6; ++i) check(i,1,0,0);
''') + "free(g_xpt); free(g_xram); return 0;\n}\n")
                harness.write_text(harness.read_text().replace("cpu.r[0] = index", f"cpu.r[{index_register}] = index"))
                executable = root / "harness"
                built = subprocess.run([compiler, "-std=gnu11", "-O2", "-I", str(ROOT / "recomp"),
                                        str(harness), "-o", str(executable)], capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                run = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
                self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
