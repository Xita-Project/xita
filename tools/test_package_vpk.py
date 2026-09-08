#!/usr/bin/env python3
"""Package boundary tests with synthetic content; no game or SDK files."""
from pathlib import Path
import tempfile
import unittest
import zipfile
from package_vpk import package


class Packaging(unittest.TestCase):
    def test_archive_and_exclusions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            content = {"build/eboot.bin": b"SCE\0test", "build/param.sfo": b"\0PSFtest",
                       "LICENSE": b"test license", "NOTICE": b"notice", "THIRD_PARTY.md": b"notices",
                       "shaders/test.gxp": b"synthetic shader", "sce_sys/icon0.png": b"icon",
                       "haloce/default.xbe": b"excluded", "haloce/maps/test.map": b"excluded",
                       "save/user.sav": b"excluded", "libshacccg.suprx": b"excluded", ".env": b"excluded"}
            for name, data in content.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            out = root / "xita.vpk"
            result = package(root, root / "build/eboot.bin", root / "build/param.sfo", out)
            self.assertEqual(result["files"], 7)
            with zipfile.ZipFile(out) as archive:
                self.assertIsNone(archive.testzip())
                self.assertEqual(archive.read("eboot.bin"), b"SCE\0test")
                self.assertEqual(archive.read("sce_sys/param.sfo"), b"\0PSFtest")
                self.assertFalse(any(archive.read(name) == b"excluded" for name in archive.namelist()))
            before = out.read_bytes()
            (root / "NOTICE").unlink()
            with self.assertRaises(ValueError):
                package(root, root / "build/eboot.bin", root / "build/param.sfo", out)
            self.assertEqual(out.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
