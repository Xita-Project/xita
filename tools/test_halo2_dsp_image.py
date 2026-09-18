"""Synthetic descriptor ranges only; no owned image bytes or keys."""
import struct
import unittest
from tools.halo2_dsp_image import describe_image


def fixture():
    data = bytearray(0x8F0)
    struct.pack_into('<6I', data, 0x800, 0, 16, 0x858, 16, 3, 0)
    struct.pack_into('<2I', data, 0x898, 2, 4096)
    for index in range(2):
        struct.pack_into('<8I', data, 0x8A0 + index * 32,
                         0x818 + index * 32, 32, 0x858 + index * 32, 32,
                         index * 8, 8, 0xC000 + index * 32, 32)
    return data


class DspDescription(unittest.TestCase):
    def test_exact_metadata_and_no_mutation(self):
        data = fixture(); before = bytes(data); report = describe_image(data)
        self.assertEqual(data, before)
        self.assertEqual((report['effect_count'], report['descriptor_offset'], report['key_table_offset']), (2, 0x898, 0x8E0))
        self.assertEqual(report['key_table_bytes'], 16)
        self.assertEqual(report['effects'][1]['scratch_offset'], 32)
        self.assertEqual(report['effects'][1]['state_bytes'], 32)
        self.assertNotIn('keys', report)

    def test_truncation_and_unknown_header(self):
        for length in (0, 0x817, 0x818, 0x897, 0x8EF):
            with self.assertRaises(ValueError): describe_image(fixture()[:length])
        for offset, value in ((0x800, 1), (0x804, 0), (0x804, 0xFFFFFFFF), (0x808, 0),
                              (0x80C, 0), (0x810, 1), (0x814, 1), (0x898, 0), (0x898, 0xFFFFFFFF), (0x89C, 0xFFFFFFFF)):
            data = fixture(); struct.pack_into('<I', data, offset, value)
            with self.assertRaises(ValueError): describe_image(data)
        with self.assertRaises(ValueError): describe_image(fixture() + bytes(4))

    def test_every_range_and_overflow(self):
        for offset, value in ((0, 0x814), (4, 0), (4, 68), (8, 0x854), (12, 68),
                              (16, 0xFFFFFFFC), (24, 0xBFFC), (28, 8192), (24, 0xFFFFFFFC)):
            data = fixture(); struct.pack_into('<I', data, 0x8A0 + offset, value)
            with self.assertRaises(ValueError): describe_image(data)
        for offset in range(0, 32, 4):
            data = fixture(); value = struct.unpack_from('<I', data, 0x8A0 + offset)[0]
            struct.pack_into('<I', data, 0x8A0 + offset, value + 1)
            with self.assertRaises(ValueError): describe_image(data)


if __name__ == '__main__': unittest.main()
