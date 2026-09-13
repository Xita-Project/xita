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
    binary = directory / 'test'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-I', str(directory),
                    str(root / 'tools/tests/constant_upload.c'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
