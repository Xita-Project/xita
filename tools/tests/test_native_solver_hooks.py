"""Native solver hook completeness, exact removal, and drift rejection."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import patch_native_4b9d0_hooks as hooks


class SolverHooks(unittest.TestCase):
    def test_complete_reversible_and_idempotent(self):
        original = hooks.S_DECL + hooks.S_CALL + hooks.S_CONT
        patched = hooks.patch_solver(original)
        self.assertEqual(hooks.patch_solver(patched), patched)
        restored = (patched.replace(hooks.S_DECL_HOOK, '')
                    .replace(hooks.S_CALL_HOOK, hooks.S_CALL)
                    .replace(hooks.S_CONT_HOOK, hooks.S_CONT))
        self.assertEqual(restored, original)

    def test_partial_duplicate_and_drift_rejected(self):
        original = hooks.S_DECL + hooks.S_CALL + hooks.S_CONT
        patched = hooks.patch_solver(original)
        for text in (original + original, original.replace('0x170CD6u', '0x170CD7u'),
                     patched + patched, patched.replace(hooks.S_CONT_HOOK, hooks.S_CONT)):
            with self.subTest(text=text), self.assertRaises(ValueError):
                hooks.patch_solver(text)


if __name__ == '__main__':
    unittest.main()
