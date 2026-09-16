#!/usr/bin/env python3
"""Check release exclusions and that review exports cannot replace originals."""
from pathlib import Path
import importlib.util
import json
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('release_audit.py'))
audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)


class ReleaseAudit(unittest.TestCase):
    def test_categories(self):
        for name in ['recomp/code_123.c', 'recomp/kernel/xk_bounds.c', 'recomp/kernel/xk_polygon_edge.c', 'recomp/kernel/xk_clip_region.c', 'recomp/kernel/xk_query_projection.c', 'shaders/halo_vs_00.cg',
                     'shaders/psdefs/example.bin', 'shaders/xv_vs_gxp.h', 'game_manifest.json',
                     'haloce/default.xbe', 'private.iso', 'libshacccg.suprx',
                     'save/checkpoint.sav', 'local/halo_ce_3925/halo_symbols.json', 'local/tools/extract-xiso', 'app/eboot.bin', 'site/img/test.png']:
            self.assertTrue(audit.path_risks(name), name)
        for name in ['runtime/main.c', 'tools/release_audit.py', 'dashboard/font.h', 'LICENSE']:
            self.assertEqual(audit.path_risks(name), [], name)

    def test_content_does_not_print_values(self):
        secret = b'gh' + b'p_' + b'a' * 36
        self.assertIn('github-token', audit.content_risks(secret))
        self.assertNotIn(secret.decode(), str(audit.content_risks(secret)))
        self.assertFalse(audit.content_risks(b'f' * 64))  # ordinary digest
        self.assertIn('long-hex-payload-review', audit.content_risks(b'f' * 128))

    def test_export_preserves_originals(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp); source = base/'src'; source.mkdir()
            files = {'runtime/main.c':'int example;\n', 'LICENSE':'example notice\n',
                     'game.xbe':'private marker', 'shaders/halo_vs_00.cg':'private translation',
                     'shaders/xv_vs_gxp.h':'private embedded translation',
                     'recomp/kernel/xk_clip.c':'generated private game math',
                     'games/example/profile.json':'{"schema_version":1}',
                     'games/example/runtime.mk':'XITA_GAME_SRCS := example.c\n'}
            for name, data in files.items():
                p=source/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_text(data)
            (source/'alias.c').symlink_to(source/'game.xbe')
            old_root, old_git = audit.ROOT, audit.git
            audit.ROOT = source; audit.git = lambda *args: '\0'.join([*files,'alias.c']).encode()
            try:
                out=base/'review'; result=audit.export_review(out)
                self.assertEqual(result['included'],4)
                self.assertFalse((out/'game.xbe').exists())
                self.assertFalse((out/'recomp/kernel/xk_clip.c').exists())
                self.assertTrue((out/'games/example/profile.json').exists())
                self.assertFalse((out/'alias.c').exists())
                self.assertFalse((out/'shaders/xv_vs_gxp.h').exists())
                self.assertFalse((out/'.git').exists())
                self.assertEqual((out/'runtime/main.c').read_bytes(),(source/'runtime/main.c').read_bytes())
                for name,data in files.items(): self.assertEqual((source/name).read_text(),data)
                with self.assertRaises(ValueError): audit.export_review(out)
                with self.assertRaises(ValueError): audit.export_review(source/'nested')
                self.assertEqual(len(json.loads((out/'SOURCE_REVIEW.json').read_text())['excluded']),5)
            finally: audit.ROOT, audit.git = old_root, old_git


if __name__ == '__main__': unittest.main()
