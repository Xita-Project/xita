"""Exercise dispatch/state preservation and fail-before-write diagnostics."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.patch_indirect_phase_timers import install, patch_body

BODY = '''void f_00164090(xctx *restrict c)
{
    if (!c->r[0]) return;
    xv_call(c, c->r[0]); return;
}
'''


class IndirectPhaseTests(unittest.TestCase):
    def test_dispatch_target_survives_callee_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            shard = root / 'code_000.c'
            unrelated = '\nvoid unrelated(xctx *c)\n{\n    xv_call(c, c->r[1]); return;\n}\n'
            shard.write_text(BODY + unrelated)
            self.assertEqual(install(root, ['00164090']), 1)
            patched = shard.read_text()
            self.assertTrue(patched.endswith(unrelated))
            self.assertEqual(install(root, ['00164090']), 0)
            self.assertEqual(shard.read_text(), patched)
            fixture = r'''
#include <stdint.h>
#include <assert.h>
typedef struct { uint32_t r[8]; } xctx;
static uint32_t began, ended, target;
static unsigned calls, events;
void xv_scene_phase_begin(uint32_t a) { assert(events++==0); began=a; }
void xv_scene_phase_end(uint32_t a) { assert(events++==2); ended=a; }
void xv_call(xctx *c,uint32_t a) {
    assert(events++==1); target=a; calls++; c->r[0]=999; c->r[4]+=4;
}
'''
            fixture += patched + r'''
int main(void) {
    xctx c={{123,0,0,0,100}};
    f_00164090(&c);
    assert(began==123 && ended==123 && target==123 && calls==1 && events==3);
    assert(c.r[0]==999 && c.r[4]==104);
    c.r[0]=0; f_00164090(&c); assert(events==3 && calls==1);
    return 0;
}
'''
            source = root / 'fixture.c'; source.write_text(fixture)
            binary = root / 'fixture'
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Werror', str(source), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_reject_before_any_write(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); shard = root / 'code_000.c'
            shard.write_text(BODY)
            with self.assertRaises(ValueError):
                install(root, ['00164090', '00123456'])
            self.assertEqual(shard.read_text(), BODY)
        for body in ('    if (ok) xv_call(c, c->r[0]); return;\n',
                     '    xv_call(c, address); return;\n',
                     '    /* XV_INDIRECT_PHASE */\n', BODY + BODY):
            with self.assertRaises(ValueError):
                patch_body(body)


if __name__ == '__main__':
    unittest.main()
