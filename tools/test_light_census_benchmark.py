#!/usr/bin/env python3
"""Production benchmark37 state and receipt tests, no game/device required."""
from pathlib import Path
import os,subprocess,tempfile
from vita_remote import parse_light_census
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as directory:
 source=(root/'runtime/main.c').read_text();begin=source.index('void xv_benchmark_present(void)')
 end=source.index('unsigned xv_settings_resolution',begin)
 (Path(directory)/'present.inc').write_text(source[begin:end])
 for enabled in (False,True):
  binary=Path(directory)/str(int(enabled));log=Path(directory)/'result.log'
  subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I'+directory,*(['-DXV_LIGHT_QUERY_CENSUS','-DXV_EXPERIMENTAL_OBJECT_JOBS'] if enabled else []),str(root/'tools/tests/light_census_benchmark.c'),'-lm','-o',str(binary)],check=True)
  subprocess.run([str(binary),str(log)],check=True)
  if not enabled:continue
  text=log.read_text();rows=parse_light_census(text)
  assert rows[1]['groups']==[360,360,360,0,240]
  assert rows[1]['query-admission'][0]==960 and rows[1]['orphans'][0]==240
  assert rows[1]['entry-admission'][2]==840 and rows[1]['query-admission'][2]==1680
  # The independently callable 8D7A6 suffix increments entry admission and
  # suffix declines, but is not an opened 8D760 owner group.
  suffix=text.replace('phase 2 entry-admission 360/','phase 2 entry-admission 361/')
  zero='/'.join(['0']*28);values=['0']*28;values[2]='2520';values[9]='1'
  suffix=suffix.replace('phase 2 declines '+ '/'.join(['0','0','2520']+['0']*25),'phase 2 declines '+ '/'.join(values))
  parse_light_census(suffix)
  bad=[text.replace('phase 1 window 120/0','phase 1 window 120/1'),text.replace('phase 2 groups 360','phase 2 groups 361'),text.replace('phase 3 budget 0','phase 3 budget 1'),text.replace('phase 2 storage 192/1216','phase 2 storage 0/0'),text.replace('phase 2 budget 0','phase 2 budget broken'),text.replace('phase 2 budget 0','phase 2 budget 0/1'),text+'[light-census-count] phase 2 budget 0\n',text.replace('[light-census-count] phase 2 budget 0\n','')]
  for invalid in bad:
   try:parse_light_census(invalid)
   except RuntimeError:pass
   else:raise AssertionError('malformed census accepted')
 print('PASS: production report schema, owner/worker/query/orphan separation and malformed/missing/duplicate/OFF-data rejection')
