#!/usr/bin/env python3
"""Profile validation and a complete synthetic-XBE lift; no commercial inputs."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from xita_recomp_core.profile import load_profile
from xbe_parse import SECTION_HEADER_FMT, XOR_KEYS


def fixture():
    # Original test program: call a custom API then return. It is deliberately
    # at the same entry address in two independently identified profiles.
    data = bytearray(0x1040)
    data[:4] = b"XBEH"
    fields = {0x104: 0x10000, 0x108: 0x1000, 0x10C: 0x2000, 0x110: 0x178,
              0x118: 0x10200, 0x11C: 1, 0x120: 0x10400,
              0x128: 0x11000 ^ XOR_KEYS["retail"]["entry"],
              0x158: XOR_KEYS["retail"]["thunk"], 0x200: 0xB0, 0x208: 0x12345678}
    for offset, value in fields.items():
        struct.pack_into("<I", data, offset, value)
    struct.pack_into(SECTION_HEADER_FMT, data, 0x400, 4, 0x11000, 64, 0x1000, 64,
                     0x10440, 0, 0, 0, bytes(20))
    data[0x440:0x446] = b".text\0"
    data[0x1000:0x1006] = b"\xe8\x0b\x00\x00\x00\xc3"  # call 0x11010; ret
    data[0x1010] = 0xC3
    profile = {"schema_version": 1, "id": "synthetic", "name": "Synthetic test",
               "binary": {"sha256": hashlib.sha256(data).hexdigest(), "title_id": "0x12345678",
                          "base_address": "0x10000", "entry_point": "0x11000", "size_of_image": "0x2000"},
               "function_overrides": {"0x11010": {"name": "TestAPI", "stack_args": 0}}}
    return bytes(data), profile


class Profiles(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="xita-profile-test-")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        data, self.doc = fixture()
        self.xbe = self.root / "test.xbe"
        self.xbe.write_bytes(data)
        self.path = self.root / "profile.json"

    def load(self, doc=None):
        self.path.write_text(json.dumps(self.doc if doc is None else doc))
        return load_profile(self.path)

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(ROOT / "xita_recomp.py"), str(self.xbe),
                               *map(str, args)], cwd=self.root, capture_output=True, text=True)

    def test_complete_lift_and_profile_isolation(self):
        for name in ("TestAPI", "DifferentAPI"):
            self.doc["function_overrides"]["0x11010"]["name"] = name
            self.load()
            out = self.root / name
            result = self.run_cli("--profile", self.path, "-o", out, "--no-data-roots")
            self.assertEqual(result.returncode, 0, result.stderr)
            code = (out / "code_000.c").read_text()
            self.assertIn(f"xv_hle_{name}", code)
            self.assertNotIn("xv_flare", code)
            report = json.loads((out / "recomp_report.json").read_text())
            self.assertEqual(report["profile"], "synthetic")
            self.assertEqual(report["unimplemented"], {})
        generic = self.root / "generic"
        result = self.run_cli("-o", generic, "--no-data-roots")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("f_00011010(c)", (generic / "code_000.c").read_text())
        self.assertEqual(json.loads((generic / "recomp_report.json").read_text())["hle_used"], [])

    def test_preflight_does_not_write(self):
        self.load()
        out = self.root / "output"
        result = self.run_cli("--profile", self.path, "--check-profile", "-o", out)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(out.exists())

    def test_output_ownership_and_stale_chunks(self):
        self.load()
        out = self.root / "output"
        result = self.run_cli("--profile", self.path, "-o", out, "--no-data-roots")
        self.assertEqual(result.returncode, 0, result.stderr)
        (out / "code_999.c").write_text("stale generated chunk")
        (out / "code_helpers.c").write_text("handwritten helper")
        result = self.run_cli("--profile", self.path, "-o", out, "--no-data-roots")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((out / "code_999.c").exists())
        self.assertEqual((out / "code_helpers.c").read_text(), "handwritten helper")
        before = (out / "code_000.c").read_bytes()
        self.doc["id"] = "second_game"
        self.load()
        result = self.run_cli("--profile", self.path, "-o", out)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("separate --outdir", result.stderr)
        self.assertEqual((out / "code_000.c").read_bytes(), before)

    def test_failed_postprocessing_preserves_output(self):
        from xita_recomp_core.output import emit_output
        out = self.root / "output"
        out.mkdir()
        (out / "code_000.c").write_text("previous complete build")
        class Emitter:
            outdir = str(out)
            def write_all(self):
                Path(self.outdir, "code_000.c").write_text("new build")
                return 1
        class Hooks:
            def postprocess(self, directory):
                raise ValueError("signature failure")
        emitter = Emitter()
        with self.assertRaisesRegex(ValueError, "signature failure"):
            emit_output(emitter, Hooks())
        self.assertEqual(emitter.outdir, str(out))
        self.assertEqual((out / "code_000.c").read_text(), "previous complete build")

    def test_other_game_cannot_select_halo_hooks(self):
        self.doc["adapter"] = "halo_ce_3925"
        self.load()
        out = self.root / "output"
        result = self.run_cli("--profile", self.path, "-o", out)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("audited executable", result.stderr)
        self.assertFalse(out.exists())

    def test_wrong_revision_is_rejected_without_output_changes(self):
        self.load()
        self.xbe.write_bytes(self.xbe.read_bytes() + b"changed")
        out = self.root / "output"
        out.mkdir()
        marker = out / "code_000.c"
        marker.write_text("preserve existing work")
        result = self.run_cli("--profile", self.path, "-o", out)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("SHA-256 mismatch", result.stderr)
        self.assertEqual(marker.read_text(), "preserve existing work")

    def test_manifest_identity_and_addresses(self):
        import xita_recomp as r
        profile = self.load()
        image = r.Image(str(self.xbe))
        profile.validate_image(image)
        for key in ("base_address", "entry_point", "kernel_thunk", "tls_address", "size_of_image"):
            before = image.m[key]
            image.m[key] += 4
            with self.assertRaisesRegex(ValueError, "manifest"):
                profile.validate_image(image)
            image.m[key] = before
        image.m["sections"][0]["raw_address"] += 1
        with self.assertRaisesRegex(ValueError, "sections"):
            profile.validate_image(image)
        for key in ("title_id", "entry_point"):
            doc = copy.deepcopy(self.doc)
            doc["binary"][key] = "0x11004"
            with self.assertRaises(ValueError):
                self.load(doc).validate_image(r.Image(str(self.xbe)))
        self.doc["roots"] = ["0x10000"]
        with self.assertRaisesRegex(ValueError, "not executable"):
            self.load().validate_image(r.Image(str(self.xbe)))

    def test_bad_profiles(self):
        changes = [{"schema_version": True}, {"schema_version": 2}, {"id": "../../escape"},
                   {"adapter": "os.system"}, {"name": ""}, {"heap_start": 1},
                   {"roots": [True]}, {"roots": [1, 1]}, {"lift": ["name();"]},
                   {"variables": {"bad-name": 0}}, {"symbols_sha256": "abc"},
                   {"function_overrides": {"0x11010": {"name": "a();", "stack_args": 0}}},
                   {"function_overrides": {"0x11010": {"name": "A", "stack_args": 65}}},
                   {"function_overrides": {"0x11010": {"name": "A", "stack_args": 0},
                                           "0x011010": {"name": "B", "stack_args": 0}}},
                   {"function_overrides": {"0x11010": {"name": "A", "stack_args": 0},
                                           "0x11020": {"name": "A", "stack_args": 1}}}]
        for change in changes:
            with self.subTest(change=change), self.assertRaises(ValueError):
                self.load({**self.doc, **change})
        self.path.write_text('{"schema_version":1,"schema_version":2}')
        with self.assertRaisesRegex(ValueError, "Duplicate JSON"):
            load_profile(self.path)

    def test_symbol_fingerprint(self):
        self.doc["symbols_sha256"] = hashlib.sha256(b"[]").hexdigest()
        profile = self.load()
        profile.validate_symbols(b"[]")
        for data in (None, b"[ ]"):
            with self.assertRaisesRegex(ValueError, "symbols"):
                profile.validate_symbols(data)

    def test_cli_checks(self):
        result = self.run_cli("--files", 0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("positive", result.stderr)
        result = subprocess.run([sys.executable, str(ROOT / "xita_recomp.py"), "--list-profiles"],
                                cwd=self.root, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("halo_ce_3925", result.stdout)


if __name__ == "__main__":
    unittest.main()
