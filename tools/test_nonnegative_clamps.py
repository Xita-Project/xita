#!/usr/bin/env python3
"""Check conservative combiner sign proofs and opt-in emission."""
import copy
import re
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from recompiler import pixelshader_recomp_gen as g


def inp(mapping='unsigned_identity', reg='v0'):
    return dict(mapping=mapping, reg=reg, channel='rgb')


def stage():
    out = dict(ab='r0', cd='r1', sum='t0', scale='identity',
               ab_dot=False, cd_dot=False, mux=False,
               ab_blue_to_alpha=False, cd_blue_to_alpha=False)
    return dict(index=0, rgb_in=[inp() for _ in range(4)],
                alpha_in=[dict(inp(), channel='alpha') for _ in range(4)],
                rgb_out=out, alpha_out=copy.deepcopy(out))


def emit(s, enabled):
    old = g.NONNEGATIVE_CLAMPS
    try:
        g.NONNEGATIVE_CLAMPS = enabled
        lines = []
        g.emit_stage(s, True, lines, set())
        return '\n'.join(lines)
    finally:
        g.NONNEGATIVE_CLAMPS = old


class ProofTests(unittest.TestCase):
    def test_unknown_signed_ranges_stay_unknown(self):
        for mapping in g.MAP_RGB:
            self.assertEqual(g.mapped_nonnegative(inp(mapping)),
                             mapping in ('unsigned_identity', 'unsigned_invert'))

    def test_zero_maps(self):
        positive = {'unsigned_identity', 'unsigned_invert', 'expand_negate',
                    'halfbias_negate', 'signed_identity', 'signed_negate'}
        for mapping in g.MAP_RGB:
            self.assertEqual(g.mapped_nonnegative(inp(mapping, 'zero')), mapping in positive)

    def test_partial_proof(self):
        self.assertEqual(g.nonnegative_results([inp(), inp(), inp('signed_identity'), inp()]), {'AB'})

    def test_default_and_scales(self):
        s = stage()
        self.assertEqual(emit(s, False).count('= clamp('), 6)
        for scale in g.SCALE:
            s['rgb_out']['scale'] = s['alpha_out']['scale'] = scale
            n = 0 if scale in ('bias', 'x2_bias') else 6
            self.assertEqual(len(re.findall(r'\b(?:r0|r1|t0)\.(?:rgb|a) = saturate\(', emit(s, True))), n)

    def test_signed_sum_retained(self):
        s = stage(); s['rgb_in'][3] = inp('signed_negate')
        text = emit(s, True)
        self.assertIn('r0.rgb = saturate(AB);', text)
        self.assertIn('r1.rgb = clamp(CD, -1.0, 1.0);', text)
        self.assertIn('t0.rgb = clamp(SUM, -1.0, 1.0);', text)

    def test_mux_and_dot(self):
        s = stage(); s['rgb_out'].update(mux=True, ab_dot=True)
        self.assertIn('t0.rgb = saturate(SUM);', emit(s, True))
        s['rgb_in'][0] = inp('expand_normal')
        self.assertIn('t0.rgb = clamp(SUM, -1.0, 1.0);', emit(s, True))


if __name__ == '__main__':
    unittest.main()
