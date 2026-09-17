#!/usr/bin/env python3
"""Production packing fixtures, source capture extraction, ARM OFF identity and Make transitions.
Owned shader headers are read from --stage, never copied into tracked files.
"""
import argparse,hashlib,json,os,shlex,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--stage',type=Path,required=True);p.add_argument('--start-at',type=int,choices=(0,1,2,3),default=0,help='Resume a failed harness phase: host, capture, ARM, Make');p.add_argument('--sdk',type=Path,default=Path.home()/'vitasdk');a=p.parse_args();out=a.output_dir.resolve();out.mkdir(parents=True,exist_ok=True);rows=json.loads((out/'commands.json').read_text()) if a.start_at and (out/'commands.json').exists() else []
 def run(args,cwd=ROOT,ok=True):
  args=list(map(str,args));r=subprocess.run(args,cwd=cwd,text=True,capture_output=True);rows.append(dict(command=args,returncode=r.returncode,stdout=r.stdout,stderr=r.stderr))
  (out/'commands.json').write_text(json.dumps(rows,indent=2)+'\n')
  if (r.returncode==0)!=ok:raise RuntimeError(r.stdout+r.stderr)
  return r
 cc=['cc','-O2','-g','-std=gnu11','-fno-strict-aliasing','-ffunction-sections','-fdata-sections','-Wall','-Werror','-Wno-unused-function','-Wno-unused-parameter','-Wno-pointer-to-int-cast','-I'+str(ROOT),'-I'+str(ROOT/'runtime'),'-I'+str(a.stage),'-idirafter',str(a.sdk/'arm-vita-eabi/include')]
 if a.start_at==0:
  for name in ('upload','prepare','shader'):
   extra=[ROOT/'runtime/xv_upload_worker.c'] if name=='prepare' else []
   for sanitized in (False,True):
    binary=out/f'{name}-{sanitized}'
    run(cc+(['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if sanitized else [])+[ROOT/f'recomp/host/packed_vertex_{name}_test.c',*extra,'-pthread','-Wl,--gc-sections','-o',binary]);run([binary])
 # Extract the complete original capture loop, including guest physical alias,
 # base/stride, source-loan descriptors and decline ordering. No copied model.
 d3d=(ROOT/'runtime/xv_d3d.c').read_text();begin=d3d.index('    _Static_assert(XV_MAX_STREAMS <= XV_VERTEX_PREPARE_STREAMS');end=d3d.index('    if (indices)\n',begin);body=d3d[begin:end]
 assert body.count('packed_vertex=XV_PACKED_PREFIX16')==1
 tail=d3d[d3d.index('    for (unsigned i=0;i<prep.count;i++) c->streams'):d3d.index('\nvoid xv_d3d_DrawVertices')]
 assert 'trace_frame()' not in tail and '&& !c->packed_vertex' in tail
 gate_start=d3d.index('#if XV_PACKED_VERTEX_LAYOUT\n#define geometry_trace');gate_end=d3d.index('#endif',gate_start)+len('#endif');gate=d3d[gate_start:gate_end]
 assert 'c->packed_vertex ? v->vs.packed_vprog : v->vs.vprog, fp' in d3d
 fixture=out/'capture.c'
 fixture.write_text('''#define XV_PACKED_VERTEX_LAYOUT 1
#define XV_VERTEX_PREPARE_TEST_NO_MAIN
#include "'''+str(ROOT/'recomp/host/vertex_prepare_test.c')+'''"
#include "runtime/xv_shader.h"
typedef struct { uint32_t Common,Data; } X_D3DResource;
static struct { uint32_t stream_guest[4];unsigned stream_stride[4]; } S;
static X_D3DResource resource[2];static uint8_t guest[32768];
static unsigned g_build_frame,hist,g_draw_vertex_refs_valid;
static xv_vertex_refs g_draw_vertex_refs;
#define XV_NUM_LISTS 3
#define XV_ONCE(...) ((void)0)
#define XV_LOG(...) ((void)0)
static int trace_frame(void) {return hist;}
'''+gate+'''
static void *xv_guest_ptr(uint32_t p) {
 if(p==1 || p==2)return &resource[p-1];
 assert(p==0x80001234u);return guest;
}
static int vertex_reference_layout(const xv_vs_desc_t *d,unsigned s,unsigned stride)
{return d->stride[s]==stride;}
static struct {unsigned ncmds,dropped,drop_attributes;} list;
#define cur_list() (&list)
typedef struct {const void *streams[4];unsigned packed_vertex;} cmd_t;
static void capture(cmd_t *c,xv_vshader_t *vs,unsigned nverts,unsigned base_vertex,const void *immediate) {
 struct {xv_vshader_t vs;} storage={.vs=*vs},*v=&storage;
 const xv_vs_desc_t *d=vs->desc;
'''+body+'''
 xv_vertex_prepare_begin(&prep);assert(xv_vertex_prepare_finish(&prep));
 for(unsigned i=0;i<prep.count;i++)c->streams[prep_stream[i]]=prep.streams[i].result;
}
int main(void) {
 xv_vs_desc_t d={.nstreams=2,.stride={32,8}};
 xv_vshader_t vs={.desc=&d,.packed_vprog=(void *)2};
 resource[0].Data=resource[1].Data=0x1234;S.stream_guest[0]=1;S.stream_guest[1]=2;
 xv_vertex_worker_override(1);xv_vertex_prepare_override(0,1);
 for(unsigned frame=0;frame<6;frame++) {
  g_build_frame=frame;xv_vertex_upload_reset(frame%3);
  for(unsigned i=0;i<sizeof guest;i++)guest[i]=(uint8_t)(i*31+frame);
  uint8_t saved[32768];memcpy(saved,guest,sizeof saved);
  cmd_t commands[4]={0};hist=0;S.stream_stride[0]=32;
  capture(&commands[0],&vs,17,3,NULL);assert(commands[0].packed_vertex==1);
  hist=1;capture(&commands[1],&vs,17,3,NULL);assert(!commands[1].packed_vertex);
  hist=0;S.stream_stride[0]=36;capture(&commands[2],&vs,17,3,NULL);assert(!commands[2].packed_vertex);
  capture(&commands[3],&vs,17,3,saved);assert(!commands[3].packed_vertex && commands[3].streams[0]==saved);
  cmd_t sparse={0};g_draw_vertex_refs_valid=1;S.stream_stride[0]=32;
  xv_vertex_refs_clear(&g_draw_vertex_refs);xv_vertex_refs_add(&g_draw_vertex_refs,0);xv_vertex_refs_add(&g_draw_vertex_refs,511);
  capture(&sparse,&vs,512,0,NULL);assert(!sparse.packed_vertex);g_draw_vertex_refs_valid=0;
  hist=1;assert(!geometry_trace(&commands[0]) && geometry_trace(&commands[1]));
  memset(guest,0,sizeof guest);S.stream_stride[0]=8;vs.packed_vprog=NULL;
  xv_vertex_upload_seal(frame%3);xv_vertex_upload_wait(frame%3);
  for(unsigned i=0;i<17;i++) {
   assert(!memcmp((const uint8_t *)commands[0].streams[0]+i*16,saved+(i+3)*32,16));
   assert(!memcmp((const uint8_t *)commands[1].streams[0]+i*32,saved+(i+3)*32,32));
   assert(!memcmp((const uint8_t *)commands[2].streams[0]+i*36,saved+(i+3)*36,36));
  }
  assert(!memcmp(commands[0].streams[1],saved+3*8,17*8));
  assert(!memcmp(sparse.streams[0],saved,512*32));
  vs.packed_vprog=(void *)2;
 }
 reset();puts("PASS production capture extraction: base vertex, physical alias, two streams, packed/raw/immediate/trace/stride switches, retained fetched bytes and six slot generations");
}
''')
 if a.start_at<=1:run(cc+['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',fixture,ROOT/'runtime/xv_upload_worker.c','-pthread','-Wl,--gc-sections','-o',out/'capture']);run([out/'capture'])
 # Real Vita compiler, changed runtime owners. OFF text equals the git parent.
 arm=[a.sdk/'bin/arm-vita-eabi-gcc','-O2','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-fno-strict-aliasing','-Wall','-Wno-unused-parameter','-I'+str(ROOT),'-I'+str(ROOT/'runtime'),'-I'+str(a.stage/'shaders'),'-I'+str(a.stage)]
 identity=[]
 if a.start_at<=2:
  for unit in ('xv_vertex_upload','xv_vertex_prepare','xv_shader','xv_d3d'):
   source=ROOT/f'runtime/{unit}.c';old=out/f'{unit}-parent.c';old.write_text(run(['git','show','7fdca4c:runtime/'+unit+'.c']).stdout)
   for mode,path in [('parent',old),('off',source),('on',source)]:
    obj=out/f'{unit}-{mode}.o';run(arm+[f'-DXV_PACKED_VERTEX_LAYOUT={int(mode=="on")}','-c',path,'-o',obj]);run([a.sdk/'bin/arm-vita-eabi-objcopy','-O','binary','-j','.text',obj,out/f'{unit}-{mode}.text'])
   before=(out/f'{unit}-parent.text').read_bytes();after=(out/f'{unit}-off.text').read_bytes();assert_lines=0
   if before!=after:
    assert unit=='xv_vertex_prepare',unit
    dumps=[run([a.sdk/'bin/arm-vita-eabi-objdump','-dr',out/f'{unit}-{m}.o']).stdout.splitlines()[3:] for m in ('parent','off')]
    assert len(dumps[0])==len(dumps[1])
    for i,(left,right) in enumerate(zip(*dumps)):
     if left==right:continue
     assert all('movs' in line and 'r1, #' in line for line in (left,right)),(left,right)
     assert '__assert_func' in '\n'.join(dumps[0][i:i+10]) and '__assert_func' in '\n'.join(dumps[1][i:i+10])
     assert_lines+=1
    assert assert_lines==6
   identity.append(dict(unit=unit,off_text_bytes=len(after),sha256=hashlib.sha256(after).hexdigest(),assert_line_number_immediates=assert_lines,on_text_bytes=(out/f'{unit}-on.text').stat().st_size))
 if a.start_at<=2:(out/'arm-identity.json').write_text(json.dumps(identity,indent=2)+'\n')
 else:identity=json.loads((out/'arm-identity.json').read_text())
 # Reuse only the established Make fixture construction, not its startup tests.
 # It copies the complete real Makefile and invokes actual host cc/ar recipes.
 template=(ROOT/'tools/test_vertex_resident_startup.py').read_text();lo=template.index('    # Real Makefile, real host compiler/ar');hi=template.index('    previous = None',lo)
 if (out/'make').exists():
  assert (out/'make/record.py').exists()
  __import__('shutil').rmtree(out/'make')
 scope={'ROOT':ROOT,'out':out,'os':os,'sys':sys,'shlex':shlex,'shutil':__import__('shutil'),'run':run};exec('def setup():\n'+template[lo:hi]+'    return locals()\nvalues=setup()\n',scope);scope.update(scope['values'])
 stage=scope['stage'];put=scope['put'];put('runtime/xv_vertex_prepare.c');put('runtime/xv_packed_vertex.h');owners=scope['owners']|{'runtime/xv_vertex_prepare.c'};targets=['build/'+s[:-2]+'.o' for s in sorted(owners)]+['build/recomp/libxita_guest.a'];base=scope['base'];previous=None;builds=[]
 for mode in (0,1,1,0,0):
  log=stage/'commands.jsonl';log.write_text('');run(base+[f'XV_PACKED_VERTEX_LAYOUT={mode}',*targets],cwd=stage);commands=[json.loads(s) for s in log.read_text().splitlines()];compiles={c[c.index('-c')+1]:c for c in commands if '-c' in c}
  want=(owners|{'recomp/code_000.c','recomp/xv_fn_table.c','recomp/xv_stubs_default.c'}) if previous is None else owners if mode!=previous else set();assert set(compiles)==want,(mode,compiles,want)
  for source,cmd in compiles.items():assert [x for x in cmd if x.startswith('-DXV_PACKED_VERTEX_LAYOUT=')]==([f'-DXV_PACKED_VERTEX_LAYOUT={mode}'] if source in owners else [])
  builds.append(dict(mode=mode,recipes=commands));previous=mode
 for bad in ('','2','-1','0 1'):
  assert 'XV_PACKED_VERTEX_LAYOUT must be 0 or 1' in run(base+['XV_PACKED_VERTEX_LAYOUT='+bad,targets[0]],cwd=stage,ok=False).stderr
  if bad in ('2','-1'):assert 'XV_PACKED_VERTEX_LAYOUT must be 0 or 1' in run(cc+['-DXV_PACKED_VERTEX_LAYOUT='+bad,'-x','c','-fsyntax-only',ROOT/'runtime/xv_packed_vertex.h'],ok=False).stderr
 (out/'receipt.json').write_text(json.dumps(dict(result='PASS',arm_text_identity=identity,make=builds,capture_sha256=hashlib.sha256(body.encode()).hexdigest(),scope='Actual uploader/worker/preparation/loader with host SDK stubs; exact production capture-loop extraction. No GPU shader execution, package or hardware test.'),indent=2)+'\n')
 print('PASS host + ASan/UBSan production fixtures, capture extraction, ARM compile/OFF text identity and real Make transitions')
if __name__=='__main__':
 if not __debug__:raise SystemExit('Run without Python -O')
 main()
