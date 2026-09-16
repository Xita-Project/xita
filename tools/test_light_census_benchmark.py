#!/usr/bin/env python3
"""Production benchmark37 state and receipt tests, no game/device required."""
from pathlib import Path
import os,subprocess,tempfile
from vita_remote import parse_light_census,parse_query_work
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
  work=parse_query_work(text)
  for row in work[3:6]:
   assert row['summary']==[1,240,240,0,120,120,0]
   assert row['counts']==[0,120,0,0,0,0,0,0,0,120]
   assert row['cost']==[0,360,0,0,0,0,0,0,0,240000]
  query_prefix='[query-work-count] phase 2 lane 1 '
  query_bad=[
   text.replace(query_prefix+'summary 1/240/240/0/120/120/0',query_prefix+'summary 1/241/240/0/120/120/0'),
   text.replace(query_prefix+'summary 1/240/240/0/120/120/0',query_prefix+'summary 2/240/240/0/120/120/0'),
   text.replace(query_prefix+'counts 0/120/',query_prefix+'counts 0/119/'),
   text.replace(query_prefix+'cost 0/360/',query_prefix+'cost 0/361/'),
   text.replace(query_prefix+'single-counts 0/0/',query_prefix+'single-counts 0/1/'),
   text.replace(query_prefix+'budgets 0/0/120/',query_prefix+'budgets 0/0/119/'),
   text.replace(query_prefix+'max-cost 0/3/',query_prefix+'max-cost 0/400/'),
   text+'[query-work-count] phase 2 lane 1 summary 1/0/0/0/0/0/0\n',
   '\n'.join(line for line in text.splitlines() if not line.startswith(query_prefix+'summary ')),
   text.replace('[query-work-count] phase 1 lane 1 summary 1/0/0/0/0/0/0','[query-work-count] phase 1 lane 1 summary 1/1/0/0/0/0/0')]
  for index,invalid in enumerate(query_bad):
   try:parse_query_work(invalid)
   except RuntimeError:pass
   else:raise AssertionError(('malformed query workload accepted',index))
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
 print('PASS: query workload prefix/depth/backedge schema and malformed/incomplete/OFF-data rejection')
