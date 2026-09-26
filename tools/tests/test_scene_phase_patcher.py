"""Ensure diagnostic insertion retains calls and handles a final shard function."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parents[1] / 'patch_scene_phase_timers.py'
SOURCE = '''void f_00000010(xctx *restrict c)
{
    XV_HLE_CALL(0x123u, untouched);
}
void f_00000020(xctx *restrict c)
{
    X_PUSH32(0x24u);
    f_00000030(c);
    XV_HLE_CALL(0x183AD0u, stream);
    XV_HLE_CALL(0x181B30u, indices);
    XV_HLE_CALL(0x1842D0u, draw);
}
'''

class PatcherTests(unittest.TestCase):
    def test_tail_call_end_precedes_return(self):
        source = SOURCE.replace('    f_00000030(c);', '    f_00000030(c); return;')
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'code_000.c'
            path.write_text(source)
            cmd = [sys.executable, str(TOOL), tmp, '--parents', '00000020',
                   '--any-call', '--tail-calls']
            result = subprocess.run(cmd, check=True, capture_output=True, text=True)
            self.assertNotIn('unwrapped guest calls', result.stdout)
            first = path.read_text()
            self.assertLess(first.index('f_00000030(c);'), first.index('xv_scene_phase_end(0x00000030u)'))
            self.assertLess(first.index('xv_scene_phase_end(0x00000030u)'), first.index('    return;'))
            retained = '\n'.join(line for line in first.splitlines() if 'extern void xv_scene_phase_' not in line) + '\n'
            self.assertEqual(retained, source.replace('f_00000030(c); return;', 'f_00000030(c);\n    return;'))
            subprocess.run(cmd, check=True, capture_output=True)
            self.assertEqual(first, path.read_text())

    def test_conditional_scope_between_push_and_call(self):
        source = SOURCE.replace('    f_00000030(c);',
            '#if ENABLE_SCOPE\n    scope_begin(c);\n#endif\n    f_00000030(c);')
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'code_000.c'
            path.write_text(source)
            ordinary = [sys.executable, str(TOOL), tmp, '--parents', '00000020']
            result = subprocess.run(ordinary, check=True, capture_output=True, text=True)
            self.assertIn('unwrapped guest calls 00000030 x1', result.stdout)
            path.write_text(source)
            cmd = [sys.executable, str(TOOL), tmp, '--parents', '00000020', '--any-call']
            subprocess.run(cmd, check=True, capture_output=True)
            first = path.read_text()
            self.assertIn('xv_scene_phase_begin(0x00000030u)', first)
            retained = '\n'.join(line for line in first.splitlines() if 'extern void xv_scene_phase_' not in line) + '\n'
            self.assertEqual(retained, source)
            subprocess.run(cmd, check=True, capture_output=True)
            self.assertEqual(first, path.read_text())

    def test_modes_and_idempotence(self):
        for any_call in (False, True):
            for hle in (False, True):
                with self.subTest(any_call=any_call, hle=hle), tempfile.TemporaryDirectory() as tmp:
                    path = Path(tmp) / 'code_000.c'
                    path.write_text(SOURCE)
                    cmd = [sys.executable, str(TOOL), tmp, '--parents', '00000020']
                    if any_call: cmd.append('--any-call')
                    if hle: cmd.append('--hle-calls')
                    subprocess.run(cmd, check=True, capture_output=True)
                    first = path.read_text()
                    subprocess.run(cmd, check=True, capture_output=True)
                    self.assertEqual(first, path.read_text())
                    self.assertEqual(first.split('void f_00000020')[0], SOURCE.split('void f_00000020')[0])
                    # Remove diagnostics: every original instruction remains in order,
                    # including the closing brace of the final function.
                    retained = '\n'.join(line for line in first.splitlines() if 'extern void xv_scene_phase_' not in line) + '\n'
                    self.assertEqual(retained, SOURCE)
                    self.assertEqual(first.count('xv_scene_phase_begin(0x'), 4 if hle else 1)
                    self.assertEqual(first.count('xv_scene_phase_end(0x'), 4 if hle else 1)

if __name__ == '__main__': unittest.main()
