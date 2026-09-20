#!/usr/bin/env python3
"""Exercise production Vita worker-ID classification without a running device."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'recomp/kernel/xk_object_jobs.c').read_text()


def function(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


code = '''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define __vita__ 1
#define WORKERS 2
static unsigned initialized, reads;
static int32_t threads[2], current;
static int32_t sceKernelGetThreadId(void) { reads++; return current; }
'''
for signature in ('static int worker_thread_id_matches(',
                  'int xv_object_is_worker_id(',
                  'int xv_object_is_worker_thread('):
    code += function(signature)
code += '''int main(void) {
    threads[0] = 19; threads[1] = 27;
    for (initialized = 0; initialized < 3; initialized++) {
        for (current = -2; current < 1024; current++) {
            int expected = initialized == 1 && (current == 19 || current == 27);
            reads = 0;
            assert(xv_object_is_worker_id(current) == expected);
            assert(reads == 0);
            assert(xv_object_is_worker_thread() == expected);
            assert(reads == (initialized == 1));
        }
    }
    initialized = 1; current = 19;
    threads[0] = 35;
    assert(!xv_object_is_worker_id(current));
    current = 35;
    assert(xv_object_is_worker_id(current));
    puts("PASS: worker identity, lifecycle, changed IDs and kernel-read counts");
}
'''
with tempfile.TemporaryDirectory(prefix='xita-worker-identity-') as directory:
    directory = Path(directory)
    path = directory / 'test.c'
    path.write_text(code)
    binary = directory / 'test'
    subprocess.run(['cc', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
