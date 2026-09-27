"""Check actual branch emission publishes cached x87 state at scheduler yields."""
import unittest
from types import SimpleNamespace
from iced_x86 import Decoder
from recompiler.xita_recomp import Emitter
from recompiler.x87_regs import Plan, State
from recompiler.core.hooks import NoGameHooks

class YieldEmission(unittest.TestCase):
    def emit(self, raw, registers=True):
        ins = Decoder(32, bytes.fromhex(raw), ip=0x1000).decode()
        em = Emitter.__new__(Emitter)
        em.hooks = NoGameHooks()
        em._lp = 'L_'
        em._x87_pos = (0x1000, 0)
        em._x87 = None
        if registers:
            plan = Plan(SimpleNamespace(entry=0x1000))
            plan.lo, plan.hi, plan.fsw = 0, 1, True
            plan.states = {em._x87_pos: State(1, 0, frozenset({0, 1}), True)}
            em._x87 = plan
        out = []
        em.lower(SimpleNamespace(blocks={ins.near_branch_target: None}), ins, out)
        return '\n'.join(out)

    def test_all_yielding_branches(self):
        for raw in ('ebfe', '75fe', 'e2fe', 'e1fe', 'e0fe'):
            with self.subTest(raw=raw):
                text = self.emit(raw)
                self.assertEqual(text.count('--c->preempt'), 1)
                self.assertLess(text.index('c->fsw = xfsw;'), text.index('xv_preempt(c);'))
                self.assertLess(text.index('xv_preempt(c);'), text.index('xfsw = c->fsw;'))
                self.assertIn('c->fsp = (xfsp0 + 7u) & 7u;', text)
                self.assertIn('X_PREEMPT();', self.emit(raw, False))

    def test_forward_jump_does_not_yield(self):
        self.assertNotIn('preempt', self.emit('eb01'))
        self.assertNotIn('preempt', self.emit('7501'))

if __name__ == '__main__':
    unittest.main()
