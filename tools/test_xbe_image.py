#!/usr/bin/env python3
"""Exercise the image CLI with Windows and UTF-8 manifests; synthetic data only."""
import codecs
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "recompiler/xbe_image.py"


class ImageEncoding(unittest.TestCase):
    def test_manifest_encodings_produce_identical_images(self):
        source = b"XBEH" + bytes(range(4, 64))
        manifest = {
            "base_address": 0x10000, "size_of_image": 128,
            "size_of_headers": 16, "path": "setup/é/game.xbe",
            "sections": [{"virtual_address": 0x10040,
                          "raw_address": 32, "raw_size": 16}],
        }
        expected = bytearray(128)
        expected[:16] = source[:16]
        expected[64:80] = source[32:48]
        expected = struct.pack("<II", 0x10000, 128) + expected
        text = json.dumps(manifest, ensure_ascii=False)
        encodings = {
            "utf8": text.encode("utf-8"),
            "utf8-bom": text.encode("utf-8-sig"),
            "powershell-utf16le": codecs.BOM_UTF16_LE + text.encode("utf-16-le"),
            "utf16be-bom": codecs.BOM_UTF16_BE + text.encode("utf-16-be"),
            "utf16le": text.encode("utf-16-le"),
            "utf32": text.encode("utf-32"),
        }
        with tempfile.TemporaryDirectory(prefix="xita-image-test-") as directory:
            root = Path(directory)
            (root / "game.xbe").write_bytes(source)
            for name, data in encodings.items():
                with self.subTest(encoding=name):
                    (root / "manifest.json").write_bytes(data)
                    result = subprocess.run(
                        [sys.executable, str(SCRIPT), "game.xbe", "manifest.json", name + ".bin"],
                        cwd=root, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual((root / (name + ".bin")).read_bytes(), expected)


if __name__ == "__main__":
    unittest.main()
