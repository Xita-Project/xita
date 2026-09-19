#!/usr/bin/env python3
"""Check the production constant upload's clipping, normalization and dirty union."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'recomp/kernel/xd3d.c').read_text()
start = source.index('void xv_hle_D3DDevice_SetVertexShaderConstant(xctx *c)')
end = source.index('/* Halo builds its register-combiner programs', start)
with tempfile.TemporaryDirectory(prefix='xita-constant-upload-') as directory:
    directory = Path(directory)
    (directory / 'constant_upload.inc').write_text(source[start:end])
    def body(text, signature):
        start = text.index(signature)
        end = text.index('{', start) + 1
        depth = 1
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        return text[start:end] + '\n'
    runtime = (root / 'recomp/xv_x86rt.c').read_text()
    header = (root / 'recomp/xv_x86rt.h').read_text()
    (directory / 'constant_read.inc').write_text(
        body(runtime, 'void x_guest_read_pages(void *dst,') +
        body(header, 'static inline __attribute__((always_inline)) void x_guest_read('))
    binary = directory / 'test'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I', str(directory),
                    str(root / 'tools/tests/constant_upload.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
