#!/usr/bin/env python3
"""Exercise production recording/replay decisions and cross-thread proof publication."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'runtime/xv_d3d.c').read_text()
def section(start, end):
    a = source.index(start)
    return source[a:source.index(end, a)]

code = section('static xv_depth_proofs g_depth_proofs;', 'static int opaque_material_override')
a = source.index('    uint8_t   kind;')
a = source.rindex('typedef struct {', 0, a)
b = source.index('/* Commands retain', a)
code += source[a:b]
code += section('static unsigned record_material(', 'static int vertex_reference_layout(')
code += section('static int material_alpha_mode(', 'static SceGxmTexture g_previous_frame_texture')
replay = section('        vs_slot_t *v = &g_vs[c->vs];', '        if (!bind_draw_textures(')
with tempfile.TemporaryDirectory(prefix='xita-depth-prepare-') as temp:
    temp = Path(temp)
    (temp/'depth_prepare.inc').write_text(code)
    (temp/'depth_replay.inc').write_text(replay)
    exe = temp/'test'
    subprocess.run(['cc','-std=c11','-O2','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-pthread','-I',str(temp),
                    '-I',str(root),str(root/'tools/tests/depth_prepare.c'),'-o',str(exe)],check=True)
    for configured, override in [(None,None),('0',None),('1',None),('1','1')]:
        env = dict(os.environ)
        env.pop('XV_DEPTH_ONLY_SHADER',None)
        env.pop('XV_ALPHA_SPECIALIZE',None)
        for name,value in [('XV_DEPTH_PREPARE',configured),('XV_SHADER_OVERRIDE',override)]:
            env.pop(name,None)
            if value is not None: env[name]=value
        subprocess.run([str(exe)],env=env,check=True)
    for disabled in ['XV_DEPTH_ONLY_SHADER','XV_ALPHA_SPECIALIZE']:
        env = {k:v for k,v in os.environ.items() if k not in
               ['XV_DEPTH_ONLY_SHADER','XV_ALPHA_SPECIALIZE','XV_SHADER_OVERRIDE']}
        env.update(XV_DEPTH_PREPARE='1');env[disabled]='0'
        subprocess.run([str(exe),'unavailable'],env=env,check=True)
