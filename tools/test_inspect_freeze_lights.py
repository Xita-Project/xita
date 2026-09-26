#!/usr/bin/env python3
import struct
import unittest
from inspect_freeze_lights import DUMP_BYTES, SAMPLE_BYTES, inspect


class CaptureTest(unittest.TestCase):
    def capture(self, next_handle=0xbeef0000):
        data = bytearray(DUMP_BYTES)
        struct.pack_into('<4I', data, 0, 0x31464c58, 1, len(data), 1)
        for i in range(2):
            o = 16 + i*SAMPLE_BYTES
            struct.pack_into('<I', data, o, 15)
            struct.pack_into('<HH', data, o+52, 2, 12)
            struct.pack_into('<I', data, o+76, 0xbeef0000)
            struct.pack_into('<I', data, o+76+2048+8, 0xbeef0001)
            struct.pack_into('<I', data, o+76+2048+12+8, next_handle)
        return data

    def test_cycle_and_termination(self):
        for handle, status in [(0xbeef0000, 'cycle_in_capture'),
                               (0xffffffff, 'terminates'), (0xbeef0002, 'out_of_range')]:
            report = inspect(self.capture(handle), [0])
            for sample in report['samples']:
                self.assertEqual(sample['chains'][0]['status'], status)
                self.assertEqual(sample['chains'][0]['indices'], [0, 1])

    def test_reject_partial_header_and_inconsistent_marker(self):
        with self.assertRaises(ValueError):
            inspect(self.capture()[:-1], [])
        data = self.capture()
        data[-1] = 1
        with self.assertRaises(ValueError):
            inspect(data, [])
        struct.pack_into('<I', data, 12, 0)
        self.assertFalse(inspect(data, [])['equal_samples'])
        data[0] = 0
        with self.assertRaises(ValueError):
            inspect(data, [])

    def test_heads_are_opt_in_and_bounded(self):
        self.assertEqual(inspect(self.capture(), [])['samples'][0]['chains'], [])
        for head in [-1, 512]:
            with self.assertRaises(ValueError):
                inspect(self.capture(), [head])


if __name__ == '__main__':
    unittest.main()
