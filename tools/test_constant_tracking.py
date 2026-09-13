#!/usr/bin/env python3
"""Run production constant setters without a GPU, SDK or owned shader tables."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

def function(source, signature):
    begin = source.index(signature)
    end = source.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end] + '\n'

source = (root / 'runtime/xv_d3d.c').read_text()
code = '''#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "xv_bytes_equal.h"
static struct { float vsc[192][4]; unsigned vsc_gen; const float (*vsc_source)[4]; } S;
static unsigned scan_constant_checks, scan_constant_reused;
static uint64_t scan_constant_bytes;
static int mode;
static void xv_d3d_draw_scan_override(int value) { mode=value; }
static int draw_scan_neon(void) { return mode; }
'''
for name in ('xv_d3d_SetVertexShaderConstant', 'xv_d3d_SetAllConstants', 'xv_d3d_SetTrackedConstants'):
    code += function(source, 'void ' + name + '(')
code += function((root / 'recomp/host/draw_state_test.c').read_text(),
                 'static void test_tracked_constants(void)')
code += '''int main(void) {
    test_tracked_constants();
    puts("PASS: 8192 production tracked sequences, both comparison paths, UI/owner changes, malformed ranges and generation wrap");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-constant-tracking-') as directory:
    directory = Path(directory)
    test = directory / 'test.c'
    test.write_text(code)
    binary = directory / 'test'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I', str(root / 'runtime'),
                    str(test), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
