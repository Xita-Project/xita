#!/usr/bin/env python3
"""Exercise production texture resolution and feedback rejection without a GPU."""
import pathlib
import subprocess
import tempfile
root = pathlib.Path(__file__).resolve().parents[1]
source = (root / 'runtime/xv_d3d.c').read_text()
start = source.index('static int bind_draw_textures(')
end = source.index('static int texture_state_override', start)
with tempfile.TemporaryDirectory(prefix='xita-draw-textures-') as directory:
    directory = pathlib.Path(directory)
    (directory / 'draw_textures.inc').write_text(source[start:end])
    (directory / 'psp2').mkdir()
    (directory / 'psp2/gxm.h').write_text('/* GXM types are supplied by the resolver fixture. */\n')
    exe = directory / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    '-I', str(directory), '-I', str(root / 'runtime'), str(root / 'tools/tests/draw_textures.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
