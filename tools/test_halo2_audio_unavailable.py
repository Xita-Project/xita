"""The unavailable-driver diagnostic must remain an explicit, pinned opt-in."""
import unittest
from unittest.mock import patch
from games.halo2_5849.hooks import Halo2HostChannelHooks, Halo2AudioUnavailableHooks


class AudioDiagnostic(unittest.TestCase):
    def test_default_has_no_audio_override(self):
        default = object.__new__(Halo2HostChannelHooks)
        diagnostic = object.__new__(Halo2AudioUnavailableHooks)
        self.assertEqual(default.function_entry(0x37D797), [])
        self.assertIn("h2_audio_unavailable", diagnostic.function_entry(0x37D797)[0])
        self.assertEqual(default.function_entry(0x3FE005), diagnostic.function_entry(0x3FE005))

    def test_extra_fingerprint_rejects_mismatched_boundary(self):
        class ChangedImage:
            def bytes_at(self, address, length):
                self.checked = (address, length)
                return b"synthetic changed boundary"
        image = ChangedImage()
        # Isolate this additional boundary gate. Whole-image rejection is also
        # covered by test_halo2_gpu_bus using a real synthetic XBE.
        with patch.object(Halo2HostChannelHooks, "__init__", return_value=None):
            with self.assertRaisesRegex(ValueError, "DirectSoundCreate fingerprint"):
                Halo2AudioUnavailableHooks(image)
        self.assertEqual(image.checked, (0x37D797, 71))


if __name__ == "__main__":
    unittest.main()
