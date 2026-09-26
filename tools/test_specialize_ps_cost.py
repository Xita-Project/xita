#!/usr/bin/env python3
"""Verify diagnostic edits preserve the alpha/discard source exactly."""
import unittest
from specialize_ps_cost import specialize, RETURN, GRAY, TAIL

SOURCE = ('float4 main() : COLOR {\n'
          '    float4 t3 = float4(0.0, 0.0, 0.0, 1.0);\n'
          '    ' + TAIL + '\n')

class CostShaderTests(unittest.TestCase):
    def test_only_final_rgb_changes(self):
        result = specialize(SOURCE)
        self.assertEqual(result.replace(GRAY, RETURN), SOURCE)
        self.assertEqual(result.count(GRAY), 1)

    def test_generated_spacing_and_comments(self):
        src = SOURCE.replace('float out_a =', 'float  out_a   =').replace(
            '    if (', '    // qualified GREATER\n    if (')
        self.assertEqual(specialize(src).replace(GRAY, RETURN), src)

    def test_reject_unsupported_source(self):
        invalid = [SOURCE.replace(' > ', ' >= '),
                   SOURCE.replace('t0.a', 't1.a'),
                   SOURCE.replace(': COLOR', ': DEPTH'),
                   SOURCE.replace('float4 t3', 'discard; float4 t3'),
                   SOURCE.replace('float4 t3', 'clip(1); float4 t3'),
                   SOURCE.replace('0.0, 0.0, 0.0, 1.0', '1.0, 1.0, 1.0, 1.0'),
                   SOURCE + RETURN,
                   SOURCE.replace(RETURN, 'return float4(1,1,1,1);')]
        for src in invalid:
            with self.subTest(src=src), self.assertRaises(ValueError):
                specialize(src)

if __name__ == '__main__':
    unittest.main()
