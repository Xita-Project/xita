#!/usr/bin/env python3
"""Exercise actual Makefile switch tracking without linking a game."""
from pathlib import Path
import subprocess
import tempfile
s=(Path(__file__).resolve().parents[1]/'Makefile').read_text()
a=s.index('# Call-local sampler stack translations;')
b=s.index('# Opt-in instruction-footprint experiment',a)
with tempfile.TemporaryDirectory() as d:
 p=Path(d)
 (p/'Makefile').write_text('BUILD=build\nRECOMP_BUILD=build/recomp\nXV_NATIVE_MATERIAL_SAMPLER=1\n.DEFAULT_GOAL=all\n'+s[a:b]+'''
all: build/recomp/kernel/xd3d.o build/other.o
build/recomp/kernel/xd3d.o:
	@mkdir -p build/recomp/kernel
	@echo '$(RECOMP_CFLAGS)' >> flags.log
	@touch $@
build/other.o:
	@echo other >> other.log
	@touch $@
''')
 previous=None;count=0
 for enabled in (0,0,1,1,0):
  subprocess.run(['make','--no-print-directory',f'XV_MATERIAL_SAMPLER_POINTERS={enabled}'],cwd=p,check=True,capture_output=True)
  count+=enabled!=previous;previous=enabled
  rows=(p/'flags.log').read_text().splitlines()
  assert len(rows)==count and rows[-1]==f'-DXV_MATERIAL_SAMPLER_POINTERS={enabled}'
  assert (p/'other.log').read_text()=='other\n'
 for flags in (['XV_MATERIAL_SAMPLER_POINTERS=2'],['XV_MATERIAL_SAMPLER_POINTERS='],['XV_MATERIAL_SAMPLER_POINTERS=1','XV_NATIVE_MATERIAL_SAMPLER=0']):
  assert subprocess.run(['make',*flags],cwd=p,capture_output=True).returncode!=0
print('PASS sampler pointer flag changes rebuild xd3d only; invalid combinations rejected')
