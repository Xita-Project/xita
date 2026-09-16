#!/usr/bin/env python3
"""Production RTT/control/interface differential tests; no game/GPU required."""
import argparse, json, os, pathlib, re, struct, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--stage',type=pathlib.Path);args=ap.parse_args()
src=(ROOT/'runtime/xv_d3d.c').read_text();shader=(ROOT/'runtime/xv_shader.c').read_text()
a=src.index('void xv_d3d_render(SceGxmContext *ctx, uint32_t frame)');b=src.index('/* The clear quad',a)
c=shader.index('int xv_fshader_embedded_no_depth(');d=shader.index('\n#endif',c)
with tempfile.TemporaryDirectory(prefix='xita-depth-store-') as tmp:
 p=pathlib.Path(tmp);(p/'replay.inc').write_text(src[a:b]);(p/'shader.inc').write_text(shader[c:d])
 programs={}
 for i in range(8):
  for variant in ('','_na','_gt'):
   if i==6 and variant=='_na':continue
   programs[f'app0:shaders/p{i}{variant}.frag.gxp']=(i!=5,0 if i==4 else 1,
      (i==1 and not variant) or (i==2 and variant=='_na') or (i==3 and variant=='_gt'))
 for i in range(4):programs[f'app0:shaders/f{i}.frag.gxp']=(1,1,i==1)
 programs['app0:shaders/ps_28CF808C_07_na.frag.gxp']=(1,1,0)
 lines=['static const struct { const char *path; SceGxmProgram data; } xv_ps_embedded[] = {']
 for name,flags in sorted(programs.items()):lines.append('{"'+name+'",{'+','.join(str(int(v)) for v in flags)+'}},')
 lines+=['};'];(p/'programs.h').write_text('\n'.join(lines))
 # Adapt only mock storage representation, not the tested lookup/proof body.
 text=(p/'shader.inc').read_text().replace('(const SceGxmProgram *)xv_ps_embedded[mid].data','&xv_ps_embedded[mid].data')
 (p/'shader.inc').write_text(text)
 for qb in (False,True):
  for sanitizer in (False,True):
   exe=p/f'test-{int(qb)}-{int(sanitizer)}'
   cmd=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-parameter',
     '-DXV_DEPTH_STORE',*(['-DXV_QUERY_BOUNDARY'] if qb else []),
     *(['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if sanitizer else []),
     '-I'+str(p),str(ROOT/'tools/tests/depth_store.c'),str(ROOT/'runtime/xv_render_profile.c'),'-o',str(exe)]
   subprocess.run(cmd,check=True)
   for queue in ('0','1'):subprocess.run([str(exe)],check=True,env={**os.environ,'XV_RT_QUEUE':queue,'ASAN_OPTIONS':'detect_leaks=1'})
 for absent in (False,True):
  exe=p/f'controller-{int(absent)}'
  subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function',
    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',*(['-DTEST_NO_DEPTH_STORE'] if absent else []),
    str(ROOT/'tools/tests/depth_store_benchmark.c'),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
if args.stage:
 # The production public API queries the same depth-replacement bit. This
 # metadata check also rejects buffer-store programs in the owned asset set.
 # Layout: Vita3K vita3k/gxm/include/gxm/types.h SceGxmProgramFlags/Program.
 table=(args.stage/'shaders/xv_ps_table.h').read_text()
 paths=set(re.findall(r'"app0:(shaders/[^\"]+\.gxp)"',table))
 paths.update(p.replace('.frag.gxp','_na.frag.gxp') for p in list(paths))
 paths.update(p.replace('.frag.gxp','_gt.frag.gxp') for p in list(paths) if re.search(r'ps_154066FD_.*(?<!_na)\.frag.gxp$',p))
 paths.update('shaders/xv_'+n+'.frag.gxp' for n in ('color','texmod','tex0','lm'))
 header=(args.stage/'shaders/xv_ps_gxp.h').read_text();checked=0
 for rel in sorted(paths):
  data=(args.stage/rel).read_bytes();assert data[:4]==b'GXP\0'
  flags=struct.unpack_from('<I',data,0x14)[0];assert flags&1 and not flags&((1<<4)|(1<<14)),rel
  entry=re.search(r'\{"app0:'+re.escape(rel)+r'", (xv_ps_bytes_\d+),',header);assert entry,rel
  arr=re.search(r'static const uint8_t '+entry[1]+r'\[\] = \{(.*?)\};',header,re.S);assert arr
  assert bytes(int(x,16)for x in re.findall(r'0x([0-9a-fA-F]{2})',arr[1]))==data,rel
  checked+=1
 print(json.dumps(dict(owned_stage=str(args.stage),depth_and_buffer_store_free_embedded_programs=checked)))
