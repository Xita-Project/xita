"""Reference-driven function-pointer table discovery for strict (no-data-roots) profiles.

A table is walked only when lifted code names its address: a vtable install
(``mov [mem], imm32``), a table address loaded or pushed as an immediate, an
indexed ``[index*4 + table]`` access, or an absolute ``call/jmp [slot]``.
The walk admits consecutive aligned words that point at executable title code
and stops at the first word that does not. No data section is scanned on its
own, so this stays reference-bounded rather than a general pointer scan.
"""
import struct
import unittest

from recompiler.xita_recomp import Discovery


class SyntheticImage:
    """Two sections: code at 0x1000 (0x1000 bytes) and .rdata at 0x2000 (0x1000 bytes)."""

    def __init__(self):
        self.code = bytearray(b"\xCC" * 0x1000)
        self.rdata = bytearray(0x1000)
        self.bss = bytearray()
        self.secs = [
            (0x1000, 0, 0x1000, 0x1000, ".text", ["EXECUTABLE"]),
            (0x2000, 0x1000, 0x1000, 0x1000, ".rdata", ["EXECUTABLE"]),
            (0x3000, 0x2000, 0, 0x1000, ".bss", ["WRITABLE"]),
        ]
        self.entry = 0x1000
        self.tls = 0
        self.kernel_thunk = 0

    def put_code(self, va, data):
        self.code[va - 0x1000:va - 0x1000 + len(data)] = data

    def put_words(self, va, words):
        for index, word in enumerate(words):
            struct.pack_into("<I", self.rdata, va - 0x2000 + index * 4, word)

    def section_of(self, va):
        for s in self.secs:
            if s[0] <= va < s[0] + max(s[2], s[3]):
                return s
        return None

    def is_code(self, va):
        return 0x1000 <= va < 0x2000

    def bytes_at(self, va, n):
        if 0x1000 <= va < 0x2000:
            return bytes(self.code[va - 0x1000:va - 0x1000 + n])
        if 0x2000 <= va < 0x3000:
            return bytes(self.rdata[va - 0x2000:va - 0x2000 + n])
        return b""

    def u32(self, va):
        data = self.bytes_at(va, 4)
        return struct.unpack("<I", data)[0] if len(data) == 4 else None


RET = b"\xC3"


def discover(image, referenced_tables=True):
    discovery = Discovery(image, {}, {}, lambda *_: None)
    discovery.referenced_tables = referenced_tables
    discovery.add_root(image.entry)
    discovery.run()
    return discovery


class ReferencedTables(unittest.TestCase):
    def setUp(self):
        self.image = SyntheticImage()
        for va in (0x1100, 0x1200, 0x1300, 0x1400, 0x1500):
            self.image.put_code(va, RET)

    def test_vtable_install_walks_code_words_and_stops_at_first_non_code(self):
        # mov dword ptr [esi], 0x2000 ; ret
        self.image.put_code(0x1000, b"\xC7\x06\x00\x20\x00\x00" + RET)
        self.image.put_words(0x2000, [0x1100, 0x1200, 0x2100, 0x1300])
        discovery = discover(self.image)
        self.assertEqual(set(discovery.functions), {0x1000, 0x1100, 0x1200})
        self.assertEqual(discovery.tables, {0x2000: 2})
        self.assertEqual(discovery.stats["table_refs"], 1)
        self.assertEqual(discovery.stats["table_roots"], 2)

    def test_disabled_by_default_and_null_terminated(self):
        self.image.put_code(0x1000, b"\xC7\x06\x00\x20\x00\x00" + RET)
        self.image.put_words(0x2000, [0x1100, 0, 0x1200])
        self.assertEqual(set(discover(self.image, referenced_tables=False).functions), {0x1000})
        self.assertEqual(set(discover(self.image).functions), {0x1000, 0x1100})

    def test_register_immediate_push_and_indexed_access(self):
        # mov eax, 0x2000 ; push 0x2010 ; call dword ptr [eax*4+0x2020] ; ret
        self.image.put_code(0x1000, b"\xB8\x00\x20\x00\x00" + b"\x68\x10\x20\x00\x00" +
                            b"\xFF\x14\x85\x20\x20\x00\x00" + RET)
        self.image.put_words(0x2000, [0x1100, 0])
        self.image.put_words(0x2010, [0x1200, 0])
        self.image.put_words(0x2020, [0x1300, 0x1400, 0x2000])
        discovery = discover(self.image)
        self.assertEqual(set(discovery.functions), {0x1000, 0x1100, 0x1200, 0x1300, 0x1400})
        self.assertEqual(discovery.tables, {0x2000: 1, 0x2010: 1, 0x2020: 2})

    def test_absolute_call_slot_admits_only_that_word(self):
        # call dword ptr [0x2004] ; ret   (slot inside a wider table: only the slot is read)
        self.image.put_code(0x1000, b"\xFF\x15\x04\x20\x00\x00" + RET)
        self.image.put_words(0x2000, [0x1100, 0x1200, 0x1300])
        discovery = discover(self.image)
        self.assertEqual(set(discovery.functions), {0x1000, 0x1200})
        self.assertEqual(discovery.tables, {0x2004: 1})

    def test_code_and_unmapped_immediates_are_not_tables(self):
        # mov dword ptr [esi], 0x1100 (code immediate: ordinary candidate) ; mov ecx, 0x3000 (.bss, no bytes)
        # mov edx, 0x9000 (outside the image) ; ret
        self.image.put_code(0x1000, b"\xC7\x06\x00\x11\x00\x00" + b"\xB9\x00\x30\x00\x00" +
                            b"\xBA\x00\x90\x00\x00" + RET)
        discovery = discover(self.image)
        self.assertEqual(set(discovery.functions), {0x1000, 0x1100})
        self.assertEqual(discovery.tables, {})

    def test_unaligned_and_implausible_entries_rejected(self):
        self.image.put_code(0x1000, b"\xC7\x06\x02\x20\x00\x00" + b"\xC7\x06\x10\x20\x00\x00" + RET)
        self.image.put_words(0x2000, [0x1100, 0])
        self.image.put_words(0x2010, [0x1180, 0x1100, 0])  # 0x1180 decodes as int3 padding
        discovery = discover(self.image)
        self.assertEqual(set(discovery.functions), {0x1000, 0x1100})
        # The walk still counts the implausible word as part of the table span.
        self.assertEqual(discovery.tables, {0x2010: 2})

    def test_tables_found_in_newly_admitted_functions_are_walked_too(self):
        # 0x1000: mov [esi], 0x2000 ; ret        table 0x2000 -> 0x1100
        # 0x1100: mov [esi], 0x2100 ; ret        table 0x2100 -> 0x1200
        self.image.put_code(0x1000, b"\xC7\x06\x00\x20\x00\x00" + RET)
        self.image.put_code(0x1100, b"\xC7\x06\x00\x21\x00\x00" + RET)
        self.image.put_words(0x2000, [0x1100, 0])
        self.image.put_words(0x2100, [0x1200, 0])
        discovery = discover(self.image)
        self.assertEqual(set(discovery.functions), {0x1000, 0x1100, 0x1200})
        self.assertEqual(discovery.tables, {0x2000: 1, 0x2100: 1})


if __name__ == "__main__":
    unittest.main()
