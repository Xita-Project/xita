from pathlib import Path
import subprocess
import tempfile
import unittest

from tools.patch_model_native_timers import patch, wrappers

BODY = '''void f_0008DDF0(xctx *restrict c)
{
#ifdef XV_NATIVE_MODEL_HIERARCHY
    { extern int xv_math_model_hierarchy(xctx *); (void)xv_math_model_hierarchy(c); }
#endif
#ifdef XV_NATIVE_OBJECT_BASIS
    { extern int xv_math_object_basis(xctx *); if (xv_math_object_basis(c)) goto done; }
#endif
    c->r[1]++;
done:
    c->r[2]++;
}
'''


class ModelNativeTimers(unittest.TestCase):
    def test_preserves_hook_results_context_and_order(self):
        out = patch(BODY)
        self.assertEqual(patch(out), out)
        self.assertEqual(out.replace(wrappers(), '').replace('timed_xv_', 'xv_'), BODY)
        fixture = '''#include <stdint.h>
#include <assert.h>
typedef struct { unsigned r[8]; } xctx;
static unsigned events, current;
void xv_scene_phase_begin(uint32_t k) { assert(events%3==0); current=k; events++; }
void xv_scene_phase_end(uint32_t k) { assert(events%3==2 && current==k); events++; }
int xv_math_model_hierarchy(xctx *c) { assert(events++%3==1); c->r[3]+=5; return 1; }
int xv_math_object_basis(xctx *c) { assert(events++%3==1); c->r[4]+=9; return c->r[0]; }
'''
        fixture += out + '''
int main(void) {
 for(unsigned take=0;take<2;take++) {
  xctx c={{take}};events=0;f_0008DDF0(&c);
#ifdef XV_NATIVE_MODEL_HIERARCHY
  assert(events==6 && c.r[3]==5 && c.r[4]==9 && c.r[1]==!take);
#else
  assert(events==0 && !c.r[3] && !c.r[4] && c.r[1]==1);
#endif
  assert(c.r[2]==1);
 }
}
'''
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'test.c').write_text(fixture)
            for flags in ([], ['-DXV_NATIVE_MODEL_HIERARCHY', '-DXV_NATIVE_OBJECT_BASIS']):
                subprocess.run(['cc', '-std=c11', '-O2', *flags, str(p/'test.c'), '-o', str(p/'test')], check=True)
                subprocess.run([str(p/'test')], check=True)

    def test_rejects_partial_missing_and_ambiguous(self):
        for body in (BODY.replace('xv_math_object_basis(c)', 'other(c)'),
                     BODY + BODY,
                     patch(BODY).replace('return result;', 'return 0;', 1),
                     BODY.replace('xv_math_object_basis(c)', 'timed_xv_math_object_basis(c)')):
            with self.assertRaises(ValueError):
                patch(body)
