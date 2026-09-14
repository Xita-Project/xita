"""Synthetic revision/selection tests; no game bytes are embedded."""
import hashlib
import subprocess
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

from games.halo2_5849.hooks import (
    AUDIO_HOST_BOUNDARIES, HOST_BOUNDARIES, Halo2AudioHostHooks, Halo2AudioUnavailableHooks,
    Halo2HostChannelHooks,
)


class Image:
    def __init__(self):
        self.parts = {address: str(address).encode() for address in AUDIO_HOST_BOUNDARIES}
        self.words = {0x417124: 0x37A14F, 0x417128: 0x37C70F}

    def bytes_at(self, address, length):
        return self.parts[address][:length]

    def u32(self, address):
        return self.words[address]

    def section_of(self, address):
        return (0, 0, 0, 0, "DSOUND" if address == 0x379F5B else ".text", ())


class AudioHooks(unittest.TestCase):
    def setUp(self):
        self.image = Image()
        self.expected = {a: (len(b), hashlib.sha256(b).hexdigest()) for a, b in self.image.parts.items()}

    def construct(self):
        with patch.object(Halo2HostChannelHooks, "__init__", return_value=None), \
                patch.dict(AUDIO_HOST_BOUNDARIES, self.expected, clear=True):
            return Halo2AudioHostHooks(self.image)

    def test_exact_boundaries_and_unknown_method_guard(self):
        hook = self.construct()
        for address in AUDIO_HOST_BOUNDARIES:
            self.assertIn("h2_audio_host_call", "".join(hook.function_entry(address)))
        guard = "".join(hook.function_entry(0x379F5B))
        self.assertIn("h2_audio_guest_entry", guard)
        self.assertNotIn("return;", guard)
        self.assertEqual(hook.function_entry(0x123456), [])
        normal = object.__new__(Halo2HostChannelHooks)
        for address in HOST_BOUNDARIES:
            self.assertEqual(hook.function_entry(address), normal.function_entry(address))
        diagnostic = object.__new__(Halo2AudioUnavailableHooks)
        self.assertEqual(normal.function_entry(0x37D797), [])
        self.assertIn("h2_audio_unavailable", "".join(diagnostic.function_entry(0x37D797)))

    def test_every_fingerprint(self):
        for address in self.image.parts:
            original = self.image.parts[address]
            self.image.parts[address] = bytes(len(original))
            with self.assertRaisesRegex(ValueError, "audio host boundary"):
                self.construct()
            self.image.parts[address] = original

    def test_vtable_rejection(self):
        for slot in self.image.words:
            original = self.image.words[slot]
            self.image.words[slot] = 0
            with self.assertRaisesRegex(ValueError, "reference vtable"):
                self.construct()
            self.image.words[slot] = original

    def test_cli_is_explicit_and_mutually_exclusive(self):
        command = [sys.executable, str(Path(__file__).resolve().parents[1] / "games/halo2_5849/prepare_boot.py"), "missing.xbe"]
        for flags, message in ((["--audio-host"], "requires --host-channel"),
                               (["--host-channel", "--audio-host", "--audio-unavailable"], "not allowed")):
            result = subprocess.run(command + flags, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertIn(message, result.stderr)


if __name__ == "__main__":
    unittest.main()
