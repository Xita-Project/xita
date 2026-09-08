#!/usr/bin/env python3
"""Guard offline recovery against unrelated caches and loss of original saves."""
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.recover_halo_checkpoint import (
    recover, recover_file, STATE_BYTES, PROFILE_BYTES, ERASED_BYTES, CRC_OFFSET,
)


class RecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cache = bytearray(STATE_BYTES)
        struct.pack_into("<I", cache, 0, 0x1FD7A64F)
        cache[4:19] = b"levels\\a10\\a10\0"
        cache[0x104:0x112] = b"01.10.12.2276\0"
        cache[0x200:0x204] = b"game"
        cache[-16:] = b"retained payload"
        cls.cache = bytes(cache)
        cls.profile = bytes(ERASED_BYTES) + cls.cache[ERASED_BYTES:]

    def test_retains_checkpoint_and_reserves_profile_length(self):
        result = recover(self.profile, self.cache)
        self.assertEqual(len(result), PROFILE_BYTES)
        self.assertEqual(result[:CRC_OFFSET], self.cache[:CRC_OFFSET])
        self.assertEqual(result[CRC_OFFSET + 4:STATE_BYTES], self.cache[CRC_OFFSET + 4:])
        self.assertEqual(result[STATE_BYTES:], bytes(PROFILE_BYTES - STATE_BYTES))
        self.assertNotEqual(result[CRC_OFFSET:CRC_OFFSET + 4], bytes(4))
        self.assertEqual(recover(self.profile + bytes(PROFILE_BYTES - STATE_BYTES), self.cache), result)

    def test_mismatched_or_incomplete_payload_is_rejected(self):
        changed = bytearray(self.cache); changed[-1] ^= 1
        for profile, cache in ((self.profile, bytes(changed)),
                               (self.profile[:ERASED_BYTES], self.cache),
                               (self.profile, self.cache[:-1]),
                               (self.profile, self.cache + bytes(PROFILE_BYTES - STATE_BYTES - 1) + b"x")):
            with self.assertRaises(ValueError):
                recover(profile, cache)

    def test_intact_or_partially_erased_profile_is_rejected(self):
        for offset in (0, CRC_OFFSET, ERASED_BYTES - 1):
            profile = bytearray(self.profile); profile[offset] = 1
            with self.assertRaises(ValueError):
                recover(bytes(profile), self.cache)

    def test_unsupported_header_is_rejected(self):
        for offset in (0, 4, 0x104):
            cache = bytearray(self.cache); cache[offset] ^= 1
            with self.assertRaises(ValueError):
                recover(self.profile, bytes(cache))

    def test_output_cannot_overwrite_inputs_or_existing_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            p, c, out = (Path(tmp) / n for n in ("profile.bin", "cache.bin", "new.bin"))
            p.write_bytes(self.profile); c.write_bytes(self.cache)
            info = recover_file(p, c, out)
            self.assertEqual(info["bytes"], PROFILE_BYTES)
            link = Path(tmp) / "input-link"; link.symlink_to(p)
            for target in (p, c, out, link):
                with self.assertRaises(FileExistsError):
                    recover_file(p, c, target)
            self.assertEqual(p.read_bytes(), self.profile)
            self.assertEqual(c.read_bytes(), self.cache)
            self.assertEqual(out.read_bytes(), recover(self.profile, self.cache))


if __name__ == "__main__":
    unittest.main()
