#!/usr/bin/env python3
"""Production UV/owner differential and primary generator gates; owned retained inputs only.

Private ARM fixture output is never tracked. Counts model instructions, not cycles/FPS.
"""
from pathlib import Path
import sys,subprocess,json,struct,hashlib,argparse
S=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(S/'tools'),str(S)]
from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE

def main():
 p=argparse.ArgumentParser();p.add_argument('--quick',action='store_true');p.add_argument('--cross-model',action='store_true')
 for name in ['xbe','manifest','retained-build','out']:p.add_argument('--'+name,type=Path,required=True)
 a=p.parse_args();O=a.out.resolve();O.mkdir(parents=True,exist_ok=True);R=a.retained_build.resolve()
 from recompiler.xita_recomp import Image
 from games.halo_ce_3925 import model_uv
 image=Image(str(a.xbe),str(a.manifest))
 def extract(path,name):
  text=path.read_text();start=text.index('void f_'+name+'(');end=text.find('\nvoid ',start+1);return text[start:end if end>=0 else None].rstrip()+'\n\n'
 hooked_bodies=[]
 for addr,unit in [(0x70110,'011'),(0xa26b0,'015')]:
  body=model_uv.strip(extract(R/('recomp/code_'+unit+'.c'),f'{addr:08X}'));hooked=model_uv.hook(image,addr,body);assert model_uv.strip(hooked)==body
  hooked_bodies.append((addr,body,hooked))
  assert model_uv.hook(image,0x56f20,body)==body
  try:model_uv.hook(image,addr,body.replace('c->r[4]', 'c->r[5]',1))
  except ValueError:pass
  else:raise AssertionError('changed source accepted')
 old=image.data;image.data=b'foreign';assert model_uv.hook(image,0x70110,body)==body;image.data=old
 pre=(R/'recomp/code_010.c').read_text().split('void f_00055555')[0]
 original=pre+extract(R/'recomp/code_010.c','00056F20')+extract(R/'recomp/code_028.c','00173F20')
 (O/'original.c').write_text(original)
 (O/'xv_recomp_protos.h').write_text((R/'recomp/xv_recomp_protos.h').read_text())
 cc='/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc'
 flags=['-O2','-g1','-std=gnu11','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-fno-strict-aliasing','-ffp-contract=off','-ffunction-sections','-fdata-sections','-fstack-usage','-DXV_MODEL_UV=1','-DXV_OWNER_PHASE','-DXV_EXPERIMENTAL_OBJECT_JOBS','-D__vita__','-I'+str(O),'-I'+str(S/'recomp'),'-I'+str(R/'recomp')]
 if a.cross_model:flags+=['-DXV_MODEL_UV_CROSS_MODEL=1']
 commands=[];objects=[]
 # Compile the full actual primary bodies, proving OFF preserves text/relocations
 # and ON imports exactly the two scoped APIs for that primary.
 for addr,body,hooked in hooked_bodies:
  section='.text.f_'+format(addr,'08X');dump=[]
  for mode,text in [('original',body),('off',hooked),('on',hooked)]:
   path=O/(f'primary-{addr:x}-{mode}.c');obj=path.with_suffix('.o');path.write_text(pre+text)
   defs=[v for v in flags if v!='-DXV_MODEL_UV_CROSS_MODEL=1'and (mode=='on'or v!='-DXV_MODEL_UV=1')]
   cmd=[cc,*defs,'-c',str(path),'-o',str(obj)];commands.append(cmd);subprocess.run(cmd,check=True)
   from elftools.elf.elffile import ELFFile
   with obj.open('rb')as f:
    elf=ELFFile(f);textbytes=elf.get_section_by_name(section).data();rel=elf.get_section_by_name('.rel'+section)
    # Symbol indices can shift with extern declaration placement; compare resolved relocation names.
    sym=elf.get_section(rel['sh_link'])if rel else None
    reloc=[(x['r_offset'],x['r_info_type'],sym.get_symbol(x['r_info_sym']).name)for x in rel.iter_relocations()]if rel else []
    dump.append((textbytes,reloc))
    imports={x.name for x in elf.get_section_by_name('.symtab').iter_symbols()if x['st_shndx']=='SHN_UNDEF'}
   expected=({'xk_model_uv_begin','xk_model_uv_end'}if addr==0x70110 else{'xk_model_uv_scope_begin','xk_model_uv_scope_end'})if mode=='on'else set()
   assert {n for n in imports if n.startswith('xk_model_uv_')}==expected
  assert dump[0]==dump[1],hex(addr)
 for src in [O/'original.c',S/'recomp/kernel/xk_model_uv.c',S/'recomp/kernel/xk_owner_phase.c',S/'tools/tests/model_uv_arm.c',S/'tools/tests/cluster_runtime_arm_imports.c']:
  obj=O/(src.stem+'.o');cmd=[cc,*flags,'-c',str(src),'-o',str(obj)];commands.append(cmd);subprocess.run(cmd,check=True);objects.append(str(obj))
 names=['arm_prepare','arm_original','arm_candidate','arm_reset','layout','arm_context_ptr','arm_scope_end','arm_nested_begin','arm_nested_end','arm_owner_case','arm_scope_begin','arm_scene_end','arm_scene_begin','arm_present','arm_pending_begin','arm_pending_finish','arm_model_original','arm_model_candidate','xk_model_uv_report']
 cmd=[cc,*flags,*objects,'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=sceKernelGetThreadId,'+','.join('--undefined='+n for n in names),'-lm','-lc','-lgcc','-o',str(O/'uv.elf')];commands.append(cmd);subprocess.run(cmd,check=True)
 m=RuntimeMachine(O/'uv.elf',True)
 def rd(a,n):return bytes(m.uc.mem_read(a,n))
 def wr(a,d):m.uc.mem_write(a,d)
 def word(a):return struct.unpack('<I',rd(a,4))[0]
 def put(a,v):wr(a,struct.pack('<I',v))
 def guest(a):return RAM+word(word(m.symbols['g_xpt'])+(a>>12)*4)+(a&4095)
 def snap():return rd(m.context,m.layout['size']),rd(RAM,SIZE)
 def restore(s):wr(m.context,s[0]);wr(RAM,s[1])
 def counters():
  vals=struct.unpack('<22I',rd(m.symbols['counts'],88));return dict(hits=sum(vals[3:5]),misses=vals[5],declines=sum(vals[13:19]))
 rows=[]
 def measured(name,fp):
  row=m.call(name,fpscr=fp)
  row['owner_thread_identity_calls']=m.by_address[m.symbols['__wrap_sceKernelGetThreadId']&~1]
  row['owner_fiber_identity_calls']=m.by_address[m.symbols['xk_os_fiber_current']&~1]
  return row
 def compare(name,s=None,fp=0x63000090,expected='hit'):
  s=s or snap();restore(s);orig=measured('arm_original',fp);ref=snap();restore(s);before=counters();cand=measured('arm_candidate',fp);out=snap();after=counters()
  diff=[]
  for n,(x,y) in enumerate(zip(ref,out)):
   if x!=y:diff.append(('context'if n==0 else'arena',[(hex(i),j,k) for i,(j,k)in enumerate(zip(x,y))if j!=k][:80]))
  row={'name':name,'original':orig,'candidate':cand,'counters':{k:after[k]-before[k]for k in before},'diff':diff};rows.append(row)
  if diff or orig['fpscr']!=cand['fpscr'] or (row['counters']!={'hits':0,'misses':0,'declines':0} if expected=='foreign' else row['counters'][{'hit':'hits','miss':'misses','decline':'declines'}[expected]]!=1):
   (O/'failure.json').write_text(json.dumps(row,indent=2));raise AssertionError((name,diff,hex(orig['fpscr']),hex(cand['fpscr']),row['counters']))
  print(name,orig['instructions'],cand['instructions'],cand['firmware_copy_bytes'],flush=True)
  return s
 m.call('arm_prepare',(0,0,0));m.call('arm_reset');s=snap();compare('canonical-cold',s,expected='miss');compare('canonical-hit',s)
 if not a.quick:
  for top in [0,3,7]:
   for fp in [0,0x63000090,0x03400090,0x03800090,0x03c0009f]:
    m.call('arm_prepare',(top,0,1));m.call('arm_reset');s=snap();compare(f'fp-{top}-{fp:x}-cold',s,fp,expected='miss');compare(f'fp-{top}-{fp:x}-hit',s,fp)
  # Different material and output addresses; nonkey CPU/scratch independently poisoned.
  m.call('arm_prepare',(0,0,0));m.call('arm_reset');s=snap();compare('independent-seed',s,expected='miss');restore(s)
  wr(guest(0x101100),rd(guest(0x100100),56));put(m.context+6*4,0x101100);put(m.context+3*4,0x91880);put(m.context+7*4,0x91890)
  for i in [0,1,2,5]:put(m.context+i*4,0x31313000+i)
  put(m.context+4,0) # ECX null is equivalent for canonical selector0
  wr(m.context+32,b'\x6c'*44)
  for i in range(8):wr(m.context+m.layout['st']+i*8,struct.pack('<d',99.5+i))
  wr(guest(0x907e0),b'\xe3'*32);wr(guest(0x91880),b'\xd5'*32)
  compare('different-material-nonkey-state')
  # Every argument is keyed, including time's otherwise-dead normalization scratch/status.
  for i,val in enumerate([0x3f900000,0x3f700000,0x3e800000,0xbe800000,0x42b40000,0x418c0001]):
   m.call('arm_prepare',(0,0,0));m.call('arm_reset');s=snap();compare(f'arg-{i}-seed',s,expected='miss');restore(s);put(guest(0x90804+i*4),val);s=snap();compare(f'arg-{i}-miss',s,expected='miss');compare(f'arg-{i}-hit',s)
  # Descriptor deviations decline to the entire original function, including trig.
  for off,val in [(4,0x40000000),(12,0x3f000000),(32,1),(44,0),(48,0x3f000000)]:
   m.call('arm_prepare',(0,0,0));m.call('arm_reset');put(guest(0x100100+off),val);compare(f'descriptor-decline-{off}',expected='decline')
  for name,off,val in [('tiny',0,1),('nan',0,0x7fc12345),('inf',0,0x7f800000)]:
   m.call('arm_prepare',(0,0,0));m.call('arm_reset');put(guest(0x90804+off),val);compare(name,expected='decline')
  for symbol in ['scope_active','diagnostic_active']:
   m.call('arm_prepare',(0,0,0));m.call('arm_reset');s=snap();compare(symbol+'-seed',s,expected='miss');restore(s);put(m.symbols[symbol],0 if symbol=='scope_active'else 1);compare(symbol+'-decline',expected='decline')
  # Physical alias, not just guest address identity.
  for kind in ['row-row','row-argument','descriptor-stack','mapped-row-argument']:
   m.call('arm_prepare',(0,0,0));m.call('arm_reset')
   if kind=='row-row':put(m.context+7*4,0x90880)
   if kind=='row-argument':put(m.context+3*4,0x90804)
   if kind=='descriptor-stack':wr(guest(0x90820),rd(guest(0x100100),56));put(m.context+6*4,0x90820);put(m.context+3*4,0x90830) # output aliases descriptor
   if kind=='mapped-row-argument':put(m.symbols['pages']+0x91*4,word(m.symbols['pages']+0x90*4));put(m.context+3*4,0x91804)
   compare(kind,expected='decline')
  # Alignment/page seams safely fall back; controls/status keys must not reuse old output.
  for sp in [0x90801,0x90ff0]:
   m.call('arm_prepare',(0,sp,0));m.call('arm_reset');compare('stack-fallback-'+hex(sp),expected='decline')
  for tag,fp,fsw in [('fcw',0x63000090,0x027f7800),('fsw',0x63000090,0x023f7890),('fpscr',0x63000080,0x023f7800)]:
   m.call('arm_prepare',(0,0,0));m.call('arm_reset');base=snap();compare(tag+'-seed',base,expected='miss');restore(base);put(m.context+m.layout['fsw'],fsw);st=snap();compare(tag+'-key-miss',st,fp,expected='miss');compare(tag+'-key-hit',st,fp)
  for rc in [1,2,3]:
   m.call('arm_prepare',(0,0,1));m.call('arm_reset');st=snap();compare('round-fzoff-'+str(rc)+'-cold',st,rc<<22,expected='miss');compare('round-fzoff-'+str(rc)+'-hit',st,rc<<22)
  # Root changes with equivalent data are safe: all relevant values re-read; no pointers cached.
  m.call('arm_prepare',(0,0,0));m.call('arm_reset');s=snap();compare('root-seed',s,expected='miss');restore(s);pt=RAM+0x700000;wr(pt,rd(m.symbols['pages'],4096));put(m.symbols['g_xpt'],pt);compare('root-change-decline',expected='decline')
 # Actual owner predicate and selected-model lifecycle: no stubbed admission.
 if not a.quick:
  for kind in range(1,8):
   m.call('arm_prepare',(0,0,0));m.call('arm_reset');base=snap();compare('owner-'+str(kind)+'-seed',base,expected='miss');restore(base);m.call('arm_owner_case',(kind,));compare('owner-'+str(kind)+'-reject',expected='decline'if kind==6 else'foreign')
  m.call('arm_prepare',(0,0,0));m.call('arm_reset');base=snap();compare('nested-seed',base,expected='miss');restore(base);m.call('arm_nested_begin');compare('nested-blocked',expected='decline');restore(base);m.call('arm_nested_end');compare('outer-stays-blocked',expected='decline');restore(base);m.call('arm_scope_end');compare('after-scope-exit',expected='decline');m.call('arm_reset');compare('new-scope-cold',base,expected='miss')
  for mode in [1,2,3]:
   m.call('arm_prepare',(0,0,0));m.call('arm_reset');put(m.symbols['root_mode'],mode);compare('captured-root-'+str(mode),expected='decline')
 # A clean model transition distinguishes the default lifetime from the opt-in.
 if not a.quick:
  m.call('arm_prepare',(0,0,0));m.call('arm_reset');base=snap();compare('clean-transition-seed',base,expected='miss');restore(base)
  m.call('arm_scope_end');m.call('arm_scope_begin');compare('clean-transition-next',base,expected='hit'if a.cross_model else'miss')
 result={'cross_model':a.cross_model,'comparisons':len(rows),'commands':commands,'hashes':{str(x):hashlib.sha256(x.read_bytes()).hexdigest()for x in [O/'original.c',S/'recomp/kernel/xk_model_uv.c',S/'tools/tests/model_uv_arm.c',S/'recomp/kernel/xk_owner_phase.c',S/'recomp/xv_x86rt.h']},'rows':rows}
 (O/'results.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',len(rows))
if __name__=='__main__':main()
