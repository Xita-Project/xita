#!/usr/bin/env python3
"""Verify production material flags invalidate only their object on changes."""
from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'Makefile').read_text()
a=s.index('XV_NATIVE_70110_PHASES ?=');b=s.index('# Native effects and material helpers',a)
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'Makefile').write_text('RECOMP_BUILD=build\nRECOMP_CFLAGS=-O2\n.DEFAULT_GOAL=all\n'+s[a:b]+'''
.PHONY: all
all: build/kernel/xk_native_70110.o build/other.o
build/kernel/xk_native_70110.o:
	@mkdir -p build/kernel
	@echo '$(RECOMP_CFLAGS)' >> flags.log
	@touch $@
build/other.o:
	@echo other >> other.log
	@touch $@
''')
 count=0;previous=None
 for phase,size in ((0,0),(0,0),(0,1),(0,1),(0,0),(1,0),(1,0)):
  subprocess.run(['make','--no-print-directory',f'XV_NATIVE_70110_PHASES={phase}',f'XV_NATIVE_70110_SIZE={size}'],cwd=p,check=True,capture_output=True)
  count+=(phase,size)!=previous;previous=(phase,size)
  rows=(p/'flags.log').read_text().splitlines();assert len(rows)==count
  flags=rows[-1].split();assert ('-Os' in flags)==bool(size)
  assert f'-DXV_NATIVE_70110_PHASES={phase}' in flags
  assert (p/'other.log').read_text()=='other\n'
 for bad in ('2','oops'):
  r=subprocess.run(['make',f'XV_NATIVE_70110_SIZE={bad}'],cwd=p,capture_output=True)
  assert r.returncode!=0 and len((p/'flags.log').read_text().splitlines())==count
print('PASS material size/phase flags rebuild exactly the affected object; invalid options rejected')
