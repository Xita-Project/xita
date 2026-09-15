"""Synthetic preparation guards; no owned program or capture is a fixture."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("h2_screen", Path(__file__).with_name("prepare_screen_shaders.py"))
h2 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(h2)


def definition():
    s = [0] * 2048
    for i in range(8):
        s[(0xA60 + 4*i)//4] = 0x10203040 + i
        s[(0xA80 + 4*i)//4] = 0x90A0B0C0 + i
    s[0x288//4] = 12  # final D=r0.rgb
    s[0x28C//4] = 28 << 8  # final G=r0.a
    s[0x1E60//4] = 0x11004
    s[0x1E70//4] = 1 | (17 << 5) | (9 << 10)
    s[0x1E74//4] = 4 | (4 << 4)
    return h2.pixel_definition(s)


class ScreenPreparation(unittest.TestCase):
    def test_synthetic_banks_and_no_source_mutation(self):
        s = [i * 0x101 for i in range(2048)]
        original = s[:]
        # Decoder validation is separate; the factor layout must not interleave
        # C0/C1 or read the old XDK constant-index mappings as hardware colors.
        d = h2.pixel_definition(s)
        self.assertEqual(s, original)
        for i, stage in enumerate(d["stages"]):
            self.assertEqual(stage["c0_map"], i)
            self.assertEqual(stage["c1_map"], i)
            def color(word):
                return "(" + ", ".join(f"{((word >> shift) & 255) / 255:.3f}" for shift in (16, 8, 0, 24)) + ")"
            self.assertEqual(stage["c0_default"], color(s[(0xA60 + 4*i)//4]))
            self.assertEqual(stage["c1_default"], color(s[(0xA80 + 4*i)//4]))

    def test_route_and_shared_generator_state(self):
        d = definition()
        before = copy.deepcopy(d)
        saved = h2.pg.SAME_C[:]
        try:
            h2.pg.SAME_C[:] = [True, True]
            source = h2.fragment_source(d)
            self.assertEqual(h2.pg.SAME_C, [True, True])
            self.assertEqual(d, before)
            self.assertIn("dot(bytes0.ar, float2(256.0, 1.0))", source)
            self.assertIn("dot(bytes0.gb, float2(256.0, 1.0))", source)
            self.assertIn("float2(640.0, 480.0)", source)
            self.assertEqual(source.count("combiner stage"), 4)
            self.assertNotIn("uniform sampler2D tex1", source)
            self.assertNotIn("uniform sampler2D tex3", source)
        finally:
            h2.pg.SAME_C[:] = saved

    def test_noncanonical_mapping_and_stage_reject(self):
        for slot in range(4):
            for mapping in range(8):
                d = definition()
                if mapping == d["textures"][slot]["dot_mapping"]:
                    continue
                d["textures"][slot]["dot_mapping"] = mapping
                with self.assertRaises(ValueError):
                    h2.fragment_source(d)
        for key in ("same_c0", "same_c1", "mux_msb"):
            d = definition(); d[key] = True
            with self.assertRaises(ValueError):
                h2.fragment_source(d)
        for key, value in (("stage_count", 3), ("warnings", ["synthetic warning"])):
            d = definition(); d[key] = value
            with self.assertRaises(ValueError):
                h2.fragment_source(d)

    def test_missing_resources_and_live_inputs_reject(self):
        for slot in range(4):
            d = definition(); d["textures"][slot]["mode"] = "CUBEMAP"
            with self.assertRaises(ValueError):
                h2.fragment_source(d)
        for register in ("v0", "v1", "fog", "t1", "t3", "ef_prod"):
            d = definition(); d["stages"][0]["rgb_in"][0]["reg"] = register
            with self.assertRaises(ValueError):
                h2.fragment_source(d)
        d = definition(); d["textures"][2]["input_stage"] = 1
        with self.assertRaises(ValueError):
            h2.fragment_source(d)

    def test_final_and_outputs_reject(self):
        for slot in "abcdefg":
            d = definition(); d["final"][slot]["reg"] = "v0"
            with self.assertRaises(ValueError):
                h2.fragment_source(d)
        for part in ("rgb_out", "alpha_out"):
            for output in ("ab", "cd", "sum"):
                d = definition(); d["stages"][0][part][output] = "t1"
                with self.assertRaises(ValueError):
                    h2.fragment_source(d)

    def test_emitter_exception_restores_shared_state(self):
        old = h2.pg.emit_stage
        saved = h2.pg.SAME_C[:]
        def fail(*args):
            raise RuntimeError("synthetic failure")
        try:
            h2.pg.SAME_C[:] = [True, False]
            h2.pg.emit_stage = fail
            with self.assertRaises(RuntimeError):
                h2.fragment_source(definition())
            self.assertEqual(h2.pg.SAME_C, [True, False])
        finally:
            h2.pg.emit_stage = old
            h2.pg.SAME_C[:] = saved

    def test_wrong_owned_image_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp); xbe = root / "image"; xbe.write_bytes(b"synthetic")
            with self.assertRaisesRegex(ValueError, "owned XBE"):
                h2.prepare(xbe, root / "missing-snapshot", root / "out")
            self.assertFalse((root / "out").exists())

    def test_wrong_probe_capture_rejects(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "synthetic"
            path.write_bytes(bytes(96))
            with self.assertRaisesRegex(ValueError, "pinned native181"):
                h2.probe_inputs(path, path)


if __name__ == "__main__":
    unittest.main()
