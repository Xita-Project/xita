#!/usr/bin/env python3
"""Retained UV callsite + real constant-publication gate, after test_model_uv.py.

Reuses its exact production objects; simulated callbacks are outside the UV helper.
"""
from pathlib import Path
import sys,subprocess,json,struct,hashlib,argparse
S=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(S/'tools'),str(S)]
from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_FPSCR

def main():
 p=argparse.ArgumentParser()
 for name in ['xbe','manifest','retained-build','out']:p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();O=a.out.resolve();R=a.retained_build.resolve()
 receipt=json.loads((O/'results.json').read_text())
 for name in ['recomp/kernel/xk_model_uv.c','recomp/kernel/xk_owner_phase.c','tools/tests/model_uv_arm.c']:
  path=S/name;assert receipt['hashes'][str(path)]==hashlib.sha256(path.read_bytes()).hexdigest(), 'rerun test_model_uv.py after source changes'
 from recompiler.xita_recomp import Image
 from games.halo_ce_3925 import model_uv
 image=Image(str(a.xbe),str(a.manifest))
 text=(R/'recomp/code_011.c').read_text();start=text.index('void f_00070110(');src=text[start:text.index('\nvoid ',start+1)]
 src=model_uv.strip(src)
 candidate=model_uv.hook(image,0x70110,src)
 header=(R/'recomp/code_010.c').read_text().split('void f_00055555')[0]
 prologue=src[src.index('{')+1:src.index('L_00070110:')]
 code=header+'void uv_cached(xctx*);void uv_frontier(xctx*);void uv_callback(xctx*);\n'
 for name,start,end in [('front','0007093C','00070975'),('back','00070DA0','00070E74')]:
  body=src[src.index('    /* '+start):src.index('    /* '+end)]
  body=body.replace('    XV_HLE_CALL(', '    uv_frontier(c);\n    XV_HLE_CALL(')
  for lane in ['original','candidate']:
   lane_src=src if lane=='original'else candidate
   b=lane_src[lane_src.index('    /* '+start):lane_src.index('    /* '+end)]
   b=b.replace('    XV_HLE_CALL(', '    uv_frontier(c);\n    XV_HLE_CALL(')
   decl=lane_src[lane_src.index('{')+1:lane_src.index('L_00070110:')]
   code+='void caller_'+name+'_'+lane+'(xctx*c){'+decl+b+'\nuv_callback(c);}\n'
 (O/'caller.c').write_text(code)
 cc='/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc';flags=['-O2','-g1','-std=gnu11','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-fno-strict-aliasing','-ffp-contract=off','-ffunction-sections','-fdata-sections','-fstack-usage','-DXV_MODEL_UV=1','-DXV_OWNER_PHASE','-DXV_EXPERIMENTAL_OBJECT_JOBS','-D__vita__','-I'+str(O),'-I'+str(S/'recomp'),'-I'+str(R/'recomp')]
 if receipt.get('cross_model'):flags+=['-DXV_MODEL_UV_CROSS_MODEL=1']
 commands=[]
 for n,path in [('caller',O/'caller.c'),('caller-fixture',S/'tools/tests/model_uv_caller_arm.c')]:
  cmd=[cc,*flags,'-c',str(path),'-o',str(O/(n+'.o'))];commands.append(cmd);subprocess.run(cmd,check=True)
 cmd=[cc,*flags,'-c',str(R/'recomp/kernel/xd3d.c'),'-o',str(O/'xd3d-sections.o')];commands.append(cmd);subprocess.run(cmd,check=True)
 names=['arm_caller_prepare','arm_front_original','arm_front_candidate','arm_back_original','arm_back_candidate','arm_reset','layout','arm_context_ptr','publication_size','arm_scope_end','arm_scope_begin','arm_scene_end','arm_scene_begin','arm_present']
 cmd=[cc,*flags,*[str(O/(n+'.o'))for n in ['original','xk_model_uv','xk_owner_phase','model_uv_arm','cluster_runtime_arm_imports','caller','caller-fixture']],str(O/'xd3d-sections.o'),'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=sceKernelGetThreadId,'+','.join('--undefined='+n for n in names),'-lm','-lc','-lgcc','-o',str(O/'caller.elf')];commands.append(cmd);subprocess.run(cmd,check=True)
 m=RuntimeMachine(O/'caller.elf',True)
 def rd(a,n):return bytes(m.uc.mem_read(a,n))
 def wr(a,d):m.uc.mem_write(a,d)
 def word(a):return struct.unpack('<I',rd(a,4))[0]
 def put(a,v):wr(a,struct.pack('<I',v))
 def counter(name):
  v=struct.unpack('<22I',rd(m.symbols['counts'],88));return sum(v[3:5])if name=='hits'else v[5]
 ps=word(m.symbols['publication_size']);frontiers=[]
 def snap():return rd(m.context,m.layout['size']),rd(RAM,SIZE),rd(m.symbols['xd3d_state'],ps)
 def restore(s):wr(m.context,s[0]);wr(RAM,s[1]);wr(m.symbols['xd3d_state'],s[2])
 def hook(uc,address,size,user):
  if address==m.symbols['uv_frontier']&~1:frontiers.append((snap(),uc.reg_read(UC_ARM_REG_FPSCR)))
 m.uc.hook_add(UC_HOOK_CODE,hook)
 rows=[]
 def compare(name,route,s,mode,expected,fp=0x63000090):
  restore(s);put(m.symbols['callback_mode'],mode);put(m.symbols['callback_calls'],0);frontiers.clear();old=m.call('arm_'+route+'_original',fpscr=fp);ref=snap();rf=list(frontiers);oldcb=word(m.symbols['callback_calls'])
  restore(s);put(m.symbols['callback_mode'],mode);put(m.symbols['callback_calls'],0);frontiers.clear();before=counter(expected);new=m.call('arm_'+route+'_candidate',fpscr=fp);out=snap();nf=list(frontiers)
  assert ref==out and rf==nf and old['fpscr']==new['fpscr'] and oldcb==word(m.symbols['callback_calls']),(name,'state')
  assert counter(expected)==before+1,(name,expected)
  rows.append(dict(name=name,original=old,candidate=new,frontiers=len(rf),callbacks=oldcb));print(name,old['instructions'],new['instructions'],flush=True)
 for back,route in enumerate(['front','back']):
  for top in [0,5]:
   m.call('arm_caller_prepare',(back,top,0));m.call('arm_reset');s=snap()
   compare(f'{route}-{top}-cold',route,s,0,'misses');compare(f'{route}-{top}-hit',route,s,0,'hits')
   compare(f'{route}-{top}-callback-live-state',route,s,3,'hits')
   compare(f'{route}-{top}-callback-scale-time',route,s,1,'hits')
   # Preserve callback mutations, rebuild only caller's next-call setup.
   changed=snap();mat=struct.unpack_from('<I',changed[0],20)[0]
   m.call('arm_caller_prepare',(back,top,0));nexts=snap();restore(nexts)
   # Apply same changed material scale and time to next invocation: original retained
   # caller prepares scale; front begins after time push so update its preseed arg too.
   def guest(a):return RAM+word(word(m.symbols['g_xpt'])+(a>>12)*4)+(a&4095)
   put(guest(mat+0x9c),0x3f400000);put(RAM+(4<<20)+0x2fc918,0x41900000)
   if not back:put(guest(0x907e8+20),0x41900000)
   nexts=snap();compare(f'{route}-{top}-after-callback-miss',route,nexts,0,'misses');compare(f'{route}-{top}-after-callback-hit',route,nexts,0,'hits')
 (O/'caller-results.json').write_text(json.dumps({'comparisons':len(rows),'commands':commands,'rows':rows,'hashes':{str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in [O/'caller.c',S/'tools/tests/model_uv_caller_arm.c',S/'recomp/kernel/xk_model_uv.c',S/'recomp/kernel/xk_owner_phase.c',R/'recomp/kernel/xd3d.c',O/'xd3d-sections.o']}},indent=2)+'\n');print('PASS',len(rows))
if __name__=='__main__':main()
