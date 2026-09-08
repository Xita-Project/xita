#!/usr/bin/env python3
"""Compile the production mesh constant-storage and binding path with GXM stubs."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'runtime/xv_d3d.c').read_text();a=s.index('/* Frame-owned vertex constants.');b=s.index('/* End frame-owned vertex constants. */',a)
with tempfile.TemporaryDirectory(prefix='xita-frame-constants-') as tmp:
 tmp=Path(tmp);(tmp/'frame_constants.inc').write_text(s[a:b]);exe=tmp/'test'
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(tmp),str(root/'tools/tests/frame_constants.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True);subprocess.run([str(exe),'disabled'],check=True)
