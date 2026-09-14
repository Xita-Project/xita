#!/usr/bin/env python3
"""Bounded vertex-work accounting through the production snapshot uploader."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sdk = Path(os.environ.get('VITASDK', str(Path.home() / 'vitasdk')))
with tempfile.TemporaryDirectory(prefix='xita-vertex-profile-') as directory:
    for name, defines in [('ordinary', []), ('profile', ['-DXV_VERTEX_PROFILE=1']),
                          ('missing-clock', ['-DXV_VERTEX_PROFILE=1', '-DNO_PROFILE_CLOCK'])]:
        binary = Path(directory) / name
        subprocess.run(['cc', '-O2', '-g', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-parameter', '-fno-strict-aliasing', '-fsanitize=address,undefined',
                        '-I' + str(root), '-I' + str(root / 'runtime'),
                        '-idirafter', str(sdk / 'arm-vita-eabi/include'), *defines,
                        str(root / 'tools/tests/vertex_profile.c'), '-lm', '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
