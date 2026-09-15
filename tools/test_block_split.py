"""No instruction may belong to two basic blocks.

When a straight-line lift runs through what a later back-edge turns into its own
block, the two blocks overlap. `split_blocks` must truncate the earlier block at
the later block's start rather than leave the tail duplicated: emitting the
overlap twice gives the two copies inconsistent flag/liveness analysis (a loop's
`dec; jne` lost its zero-flag store in one copy and ran billions of iterations).
"""
import struct
import unittest

from recompiler.xita_recomp import Discovery


class CodeImage:
    """One executable section at 0x1000; bytes provided by the test."""

    def __init__(self, code, base=0x1000):
        self.base = base
        self.code = bytes(code)
        self.secs = [(base, 0, len(self.code), len(self.code), ".text", ["EXECUTABLE"])]
        self.entry = base
        self.tls = 0
        self.kernel_thunk = 0

    def section_of(self, va):
        s = self.secs[0]
        return s if s[0] <= va < s[0] + s[2] else None

    def is_code(self, va):
        return self.base <= va < self.base + len(self.code)

    def bytes_at(self, va, n):
        if self.base <= va < self.base + len(self.code):
            return self.code[va - self.base: va - self.base + n]
        return b""

    def u32(self, va):
        d = self.bytes_at(va, 4)
        return struct.unpack("<I", d)[0] if len(d) == 4 else None


def discover(code):
    d = Discovery(CodeImage(code), {}, {}, lambda *a: None)
    d.add_root(0x1000)
    d.run()
    return d.functions[0x1000]


def block_ips(fn):
    return [i.ip for b in fn.blocks.values() for i in b.insns]


class BlockSplit(unittest.TestCase):
    def test_loop_body_reached_by_fallthrough_is_not_duplicated(self):
        # 1000 inc eax; 1001 dec ecx; 1002 jne 1001; 1004 ret
        fn = discover(b"\x40\x49\x75\xFD\xC3")
        ips = block_ips(fn)
        self.assertEqual(sorted(ips), sorted(set(ips)), "an instruction is in two blocks")
        # the loop body 1001..1002 lives in exactly one block, entered by fallthrough and the back edge
        self.assertIn(0x1001, fn.blocks)
        self.assertEqual([i.ip for i in fn.blocks[0x1000].insns], [0x1000])
        self.assertEqual(fn.blocks[0x1000].succ, [0x1001])
        self.assertEqual([i.ip for i in fn.blocks[0x1001].insns], [0x1001, 0x1002])

    def test_ordinary_midblock_target_still_splits(self):
        # 1000 inc eax; 1001 inc eax; 1002 jne 1001; 1004 ret  -- 1001 is a target but
        # was never lifted as its own block first; the classic split must still happen.
        fn = discover(b"\x40\x40\x75\xFD\xC3")
        ips = block_ips(fn)
        self.assertEqual(sorted(ips), sorted(set(ips)))
        self.assertIn(0x1001, fn.blocks)
        self.assertEqual([i.ip for i in fn.blocks[0x1000].insns], [0x1000])

    def test_straight_line_function_has_no_overlap(self):
        # 1000 inc eax; 1001 inc ecx; 1002 ret
        fn = discover(b"\x40\x41\xC3")
        ips = block_ips(fn)
        self.assertEqual(sorted(ips), [0x1000, 0x1001, 0x1002])


class SwitchTables(unittest.TestCase):
    """jmp [reg*4 + table] must recover sparse tables with null holes."""

    def _disc(self, code, table_bytes, table_va=0x2000):
        img = CodeImage(code)
        # extend the image with a data area holding the table
        pad = table_va - (img.base + len(img.code))
        img.code = img.code + b"\x00" * pad + bytes(table_bytes)
        img.secs = [(img.base, 0, len(img.code), len(img.code), ".text", ["EXECUTABLE"])]
        d = Discovery(img, {}, {}, lambda *a: None)
        d.add_root(0x1000)
        d.run()
        return d, img

    def test_sparse_table_skips_null_holes(self):
        # jmp dword ptr [eax*4 + 0x2000]
        code = b"\xFF\x24\x85\x00\x20\x00\x00"
        # targets must be executable (< table_va, inside code): use 0x1000,0x1002,... near entry
        import struct
        tbl = b"".join(struct.pack("<I", v) for v in
                       [0x1000, 0, 0, 0x1002, 0, 0x1004, 0, 0, 0x1006, 0xDEADBEEF])
        d, img = self._disc(code, tbl)
        fn = d.functions[0x1000]
        targets = fn.switch_tables.get(0x1000, [])
        idx = {i: t for i, t in targets}
        self.assertEqual(idx, {0: 0x1000, 3: 0x1002, 5: 0x1004, 8: 0x1006})
        self.assertNotIn(9, idx)     # 0xDEADBEEF is not code: table ends there


if __name__ == "__main__":
    unittest.main()
