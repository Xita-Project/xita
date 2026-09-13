"""Synthetic bounds and revision guards for the observed D3D callback walk."""
import hashlib
import unittest
from unittest.mock import patch
from games.halo2_5849 import prepare_boot


class SyntheticImage:
    def __init__(self):
        self.code = b"synthetic caller"
        self.targets = {slot: 0x1000 + index * 16
                        for index, slot in enumerate(range(0x403AF8, 0x403B70, 4))}
        self.targets[0x403B40] = None  # skipped slot is never inspected as a root
        self.bad_code = None
        self.section_name = "D3D"

    def bytes_at(self, address, length):
        assert address == 0x200 and length == len(self.code)
        return self.code

    def u32(self, slot):
        return self.targets[slot]

    def is_code(self, target):
        return target != self.bad_code

    def section_of(self, target):
        return (0, 0, 0, 0, self.section_name)


class CallbackRoots(unittest.TestCase):
    def test_xpp_two_level_walk(self):
        image = SyntheticImage()
        image.section_name = "XPP"
        image.targets = {slot: 0x2000 + n * 24
                         for n, slot in enumerate(range(0x4086D4, 0x4086EC, 4))}
        image.targets.update({0x2004 + n * 24: 0x5000 + (n % 3) * 16 for n in range(6)})
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "XPP_CALLBACK_WALK", spec):
            self.assertEqual(prepare_boot.host_device_callback_roots(image), {0x5000, 0x5010, 0x5020})
            image.targets[0x4086E8] = 0  # original walk skips null descriptors
            self.assertEqual(len(prepare_boot.host_device_callback_roots(image)), 3)
            image.targets[0x4086D4] = 0x2001
            with self.assertRaisesRegex(ValueError, "descriptor"):
                prepare_boot.host_device_callback_roots(image)
            image.targets[0x4086D4] = 0x2000
            image.targets[0x2004] = None
            with self.assertRaisesRegex(ValueError, "callback"):
                prepare_boot.host_device_callback_roots(image)
            image.targets[0x2004] = 0x5000; image.bad_code = 0x5000
            with self.assertRaisesRegex(ValueError, "callback"):
                prepare_boot.host_device_callback_roots(image)
        with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
            with patch.object(prepare_boot, "XPP_CALLBACK_WALK", (0x200, len(image.code), "0" * 64)):
                prepare_boot.host_device_callback_roots(image)

    def test_exact_walk_and_invalid_targets(self):
        image = SyntheticImage()
        spec = (0x200, len(image.code), hashlib.sha256(image.code).hexdigest())
        with patch.object(prepare_boot, "HOST_CALLBACK_WALK", spec):
            roots = prepare_boot.host_channel_callback_roots(image)
            self.assertEqual(len(roots), 29)
            self.assertIn(image.targets[0x403AF8], roots)
            self.assertIn(image.targets[0x403B6C], roots)
            for target in (None, 0):
                image.targets[0x403B6C] = target
                with self.assertRaisesRegex(ValueError, "invalid target"):
                    prepare_boot.host_channel_callback_roots(image)
            image.targets[0x403B6C] = 0xDEAD
            image.bad_code = 0xDEAD
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.host_channel_callback_roots(image)
            image.bad_code = None
            image.section_name = "DATA"
            with self.assertRaisesRegex(ValueError, "invalid target"):
                prepare_boot.host_channel_callback_roots(image)

    def test_reject_changed_caller(self):
        image = SyntheticImage()
        with patch.object(prepare_boot, "HOST_CALLBACK_WALK", (0x200, len(image.code), "0" * 64)):
            with self.assertRaisesRegex(ValueError, "fingerprint mismatch"):
                prepare_boot.host_channel_callback_roots(image)


if __name__ == "__main__":
    unittest.main()
