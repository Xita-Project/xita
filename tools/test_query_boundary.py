#!/usr/bin/env python3
"""Production RTT + completion/history, plus real pump/acquisition and controller tests."""
import os,pathlib,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
s=(root/'runtime/xv_d3d.c').read_text()
a=s.index('void xv_d3d_render(SceGxmContext *ctx, uint32_t frame)');b=s.index('/* The clear quad',a)
c=s.index('void xv_d3d_visibility_complete(');d=s.index('static SceGxmBlendFactor alpha_blend_factor',c)
with tempfile.TemporaryDirectory(prefix='xita-query-boundary-') as tmp:
 p=pathlib.Path(tmp);(p/'placement_replay.inc').write_text(s[a:b]);(p/'query_complete.inc').write_text(s[c:d].replace('void xv_d3d_visibility_complete(', 'static void complete_exact('))
 exe=p/'test'
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-address','-DXV_QUERY_BOUNDARY','-DXV_FLARE_QUERY_OVERLAP','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie','-I'+str(p),str(root/'tools/tests/query_boundary.c'),str(root/'runtime/xv_render_profile.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,env={**os.environ,'XV_RT_QUEUE':'1','ASAN_OPTIONS':'detect_leaks=1'})
 for absent in (False,True):
  exe=p/('benchmark-absent' if absent else 'benchmark')
  subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',*(['-DTEST_NO_QUERY_BOUNDARY'] if absent else []),str(root/'tools/tests/query_boundary_benchmark.c'),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
for timing in ('0','1'):
 subprocess.run(['python3',str(root/'tools/test_frame_completion.py')],check=True,env={**os.environ,'TEST_QUERY_BOUNDARY':'1','TEST_GPU_PACKET_TIMING':timing})
