#!/usr/bin/env python3
"""Check the constrained capture rewrite, including executed RMW ordering."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

import query_memory_capture as capture


class CaptureRewrite(unittest.TestCase):
    def test_store_evaluation(self):
        source = '''
        X_M32(0) = X_M32(0) + 1;
        X_W32(0) = (X_M32(0) == 42 ? X_M32(0) : 0);
        '''
        rewritten, count = capture.stores(source)
        self.assertEqual(count, 2)
        # Store notification must follow all RHS reads, including same-value
        # writes; otherwise read-before-write dependencies silently disappear.
        program = r'''
        #include <assert.h>
        static unsigned value=41, reads, writes;
        static unsigned load(unsigned a) { assert(a==0); ++reads; return value; }
        static void store(unsigned a,unsigned v) {
            assert(a==0); assert(reads==(writes ? 3u : 1u)); ++writes; value=v;
        }
        #define X_M32(a) load(a)
        #define NQ_STORE32(a,v) store(a,v)
        #define NQ_STOREW32(a,v) store(a,v)
        int main(void) {
        ''' + rewritten + '\nassert(value==42 && writes==2); return 0; }'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'case.c').write_text(program)
            subprocess.run(shlex.split(os.environ.get('CC', 'cc')) +
                ['-std=c11', '-Wall', '-Wextra', '-Werror', str(path/'case.c'),
                 '-o', str(path/'case')], check=True)
            subprocess.run([str(path/'case')], check=True)

    def test_comments_and_nested_rhs(self):
        source = '/* X_M32(a)=1; */ X_M32(a) = f((b+1), (int[]){2,3});'
        text, count = capture.stores(source)
        self.assertEqual(count, 1)
        self.assertIn('NQ_STORE32(a, (f((b+1), (int[]){2,3})))', text)
        self.assertIn('/* X_M32(a)=1; */', text)
        self.assertEqual(capture.stores('if (X_M32(a)==1) f();')[1], 0)

    def test_reject_unreviewed_store(self):
        for source in ('X_M32(a)++;', 'X_M32(a) += 1;', 'X_M32(a) = f('):
            with self.assertRaises(ValueError):
                capture.stores(source)

    def test_explicit_capture_propagation(self):
        text, definitions = capture.parameters(
            'void x_pop32(xctx *c) { x_guest_read(&v,a,4); }')
        self.assertEqual(definitions, ['x_pop32'])
        self.assertIn('x_pop32(XvQueryMemory *nq_capture, xctx *c)', text)
        self.assertIn('x_guest_read(nq_capture, &v,a,4)', text)

    def test_source_identity_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in capture.PINS:
                path = root/name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('unreviewed source')
            with self.assertRaisesRegex(ValueError, 'reviewed source inventory'):
                capture.generate(root)


if __name__ == '__main__':
    unittest.main()
