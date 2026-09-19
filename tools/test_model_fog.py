#!/usr/bin/env python3
"""Production fog-cache differential gate using an owned retained ARM oracle.

Requires --xbe, --manifest, --retained-build and --out. Never publishes generated
code or game bytes. ARM instruction counts are a model, not Vita FPS evidence.
"""
from pathlib import Path
import argparse,sys,subprocess,json,struct,hashlib,random,os,re
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT)]
from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE
from unicorn.arm_const import UC_ARM_REG_FPSCR
from recompiler import xita_recomp as recomp
from games.halo_ce_3925 import model_fog
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['xbe','manifest','retained-build','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();O=a.out.resolve();O.mkdir(parents=True,exist_ok=True);B=a.retained_build.resolve()
    image=recomp.Image(str(a.xbe),str(a.manifest))
    source=(B/'recomp/code_011.c').read_text();body=source[source.index('void f_00070110('):];body=body[:body.index('\nvoid ')]
    hooked=model_fog.hook(image,0x70110,body)
    assert model_fog.strip(hooked)==body and hooked!=body
    assert model_fog.hook(image,0x70a42,body)==body
    try:model_fog.hook(image,0x70110,body.replace('0xA0u','0xA1u',1))
    except ValueError:pass
    else:raise AssertionError('changed primary body accepted')
    read=image.bytes_at
    image.bytes_at=lambda p,n:bytes([read(p,n)[0]^1])+read(p,n)[1:] if p==0x70a42 else read(p,n)
    try:model_fog.hook(image,0x70110,body)
    except ValueError:pass
    else:raise AssertionError('changed fog bytes accepted')
    image.bytes_at=read
    original_data=image.data;image.data=b'changed image';assert model_fog.hook(image,0x70110,body)==body;image.data=original_data
    # Preserve the exact retained body as cold oracle; pin it and the actual child
    # object into the receipt. Generated hook bytes are tested separately below.
    region=body[body.index('    /* 00070A42 '):body.index('L_00070D09:')].replace('goto L_00070D12;','return;')
    prefix=source[:source.index('void f_')]
    header='#include "xv_x86rt.h"\nvoid f_000118D0(xctx*);void f_00011B60(xctx*);void f_00011BD0(xctx*);\nvoid fog_original(xctx*c){\n'
    (O/'fog-original.c').write_text(header+region+'}\n')
    cc=os.environ.get('ARM_CC','/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    flags=['-O2','-g1','-std=gnu11','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-fno-strict-aliasing','-ffp-contract=off','-ffunction-sections','-fdata-sections','-fstack-usage','-DXV_MODEL_FOG=1','-DXV_OWNER_PHASE','-I'+str(ROOT/'recomp')]
    commands=[]
    for name,path in [('fog-original',O/'fog-original.c'),('fixture',ROOT/'tools/tests/model_fog_arm.c'),('fog-cache',ROOT/'recomp/kernel/xk_model_fog.c'),('imports',ROOT/'tools/tests/cluster_runtime_arm_imports.c')]:
     cmd=[cc,*flags,'-c',str(path),'-o',str(O/(name+'.o'))];commands.append(cmd);subprocess.run(cmd,check=True)
    exports=['arm_prepare','arm_original','arm_candidate','arm_reset','arm_disable','arm_enable','layout','arm_context_ptr']
    cmd=[cc,*flags,str(B/'build/recomp/code_000.o'),*[str(O/(n+'.o'))for n in ['fog-original','fixture','fog-cache','imports']],'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--unresolved-symbols=ignore-all,'+','.join('--undefined='+n for n in exports),'-lm','-lc','-lgcc','-o',str(O/'fog.elf')];commands.append(cmd);subprocess.run(cmd,check=True)
    # Both full generated functions preprocess identically with the feature off.
    for name,text in [('original',body),('hooked',hooked)]:
     (O/(name+'-off.c')).write_text('#include "xv_recomp_protos.h"\n'+text)
    pre=[subprocess.check_output([cc,'-E','-P','-I'+str(B/'recomp'),str(O/(n+'-off.c'))])for n in ['original','hooked']]
    assert pre[0]==pre[1],'feature-off changed original translation'
    m=RuntimeMachine(O/'fog.elf',True)
    def rd(a,n):return bytes(m.uc.mem_read(a,n))
    def wr(a,d):m.uc.mem_write(a,d)
    def word(a):return struct.unpack('<I',rd(a,4))[0]
    def put(a,v):wr(a,struct.pack('<I',v))
    def guest(a):return RAM+word(m.symbols['pages']+(a>>12)*4)+(a&4095)
    def snapshot():return rd(m.context,m.layout['size']),rd(RAM,SIZE)
    def restore(s):wr(m.context,s[0]);wr(RAM,s[1])
    def counters():return {n:word(m.symbols[n])for n in ['hits','misses','declines']}
    def compare(name,prepared=None,fpscr=0,expected='hit'):
     s=prepared or snapshot();restore(s);orig=m.call('arm_original',fpscr=fpscr);ref=snapshot();restore(s);before=counters();cand=m.call('arm_candidate',fpscr=fpscr);out=snapshot();after=counters()
     if out!=ref:
      for n,(a,b)in enumerate(zip(ref,out)):
       if a!=b: print('DIFF',name,'context'if n==0 else'arena',[(hex(i),x,y)for i,(x,y)in enumerate(zip(a,b))if x!=y][:40],flush=True)
      raise AssertionError(name)
     assert orig['fpscr']==cand['fpscr'],(name,hex(orig['fpscr']),hex(cand['fpscr']))
     delta={n:after[n]-before[n]for n in before}
     if expected=='bypass':assert not any(delta.values()),(name,delta)
     else:assert delta[{'hit':'hits','miss':'misses','decline':'declines'}[expected]]==1,(name,delta,expected)
     row={'name':name,'outcome':expected,'original':orig,'candidate':cand,'counters':delta};rows.append(row);return s
    rows=[]
    for variant in range(4):
     for top in [0,5]:
      m.call('arm_reset');m.call('arm_prepare',(variant,top,0));s=snapshot()
      compare(f'v{variant}-top{top}-cold',s,expected='miss');compare(f'v{variant}-top{top}-warm',s)
      # All nonkey CPU state and all scratch bytes may differ at a hit.
      restore(s)
      for i in range(8):
       if i!=4:put(m.context+i*4,0xabab0000+i)
      # x87 slots not overwritten must retain the new invocation's values.
      for i in range(8):wr(m.context+m.layout['st']+i*8,struct.pack('<d',99.5+i))
      wr(m.context+32,bytes([0x3c])*44) # fs_base, DF and incoming lazy flags
      put(m.context+m.layout['preempt'],7)
      for off in range(-36,0):wr(guest(0x90800+off),b'\xcc')
      for off in list(range(0x10,0x30))+list(range(0xa0,0xac)):wr(guest(0x90800+off),b'\xdd')
      compare(f'v{variant}-top{top}-independent-state',expected='hit')
    # Individual arithmetic input mutations must miss, then hit.
    addr=[0x90848,0x1f0a68,0x1f0a78,0x2fc6c8,0x2fc6cc,0x2fc6d0,0x2fc8ac,0x2fc8b0,0x2fc8b4,0x2fc8b8,0x2fc8bc,0x2fc8c0,0x2fc8c8,0x2fc8cc,0x2fc8d0,0x2fc8d4,0x2fc8d8,0x2fc8dc,0x2fc8e0]
    m.call('arm_reset');m.call('arm_prepare',(0,0,0));base=snapshot();compare('key-seed',base,expected='miss')
    for i,a in enumerate(addr):
     restore(base);put(guest(a),struct.unpack('<I',struct.pack('<f',.875+i*.03125))[0]);s=snapshot();compare(f'key-{i}-miss',s,expected='miss');compare(f'key-{i}-hit',s)
    restore(base);put(RAM+(4<<20)+0x2fc8a8,2);s=snapshot();compare('image-flag-miss',s,expected='miss');compare('image-flag-hit',s)
    # Control-key and safely unsupported floating inputs.
    for name,offset,value,fpscr,expected in [('fsw',m.layout['fsw'],0x027f1111,0,'miss'),('fcw',m.layout['fsw'],0x037f3210,0,'decline'),('fpscr-status',None,None,0x10,'miss'),('fpscr-round',None,None,0x00400000,'decline')]:
     restore(base)
     if offset is not None:put(m.context+offset,value)
     s=snapshot();compare(name,s,fpscr,expected)
    for value in [0x7fc00000,0x7f800000,1,0x47800001]:
     restore(base);put(guest(addr[0]),value);compare(f'decline-depth-{value:x}',expected='decline')
    restore(base);put(guest(0x2fc8c0),0);compare('decline-zero-denominator',expected='decline')
    restore(base);put(guest(0x2fc8c0),word(guest(0x2fc8bc)));compare('decline-equal-denominator',expected='decline')
    # Changed stack address and mapped physical stack page each force a miss.
    for sp in [0x91800,0x90800]:
     m.call('arm_prepare',(0,0,sp));s=snapshot();compare('stack-'+hex(sp),s,expected='miss');compare('stack-warm-'+hex(sp),s)
    # Page-split stacks decline without any candidate partial write.
    m.call('arm_prepare',(0,0,0x90ff0));compare('stack-split',expected='decline')
    # Stack remapping with unchanged guest ESP must miss; coarse alias guards decline.
    for page,expect in [(0x120000,'miss'),(0x121000,'miss'),(0x2fd000,'decline'),(0x6fc000,'decline')]:
     m.call('arm_prepare',(0,0,0));put(m.symbols['pages']+0x90*4,page);put(guest(0x90848),0x41200000)
     s=snapshot();compare('mapped-stack-'+hex(page),s,expected=expect)
     if expect=='miss':compare('mapped-stack-warm-'+hex(page),s)
    # Changed root identity with equivalent mappings/data also invalidates the key.
    m.call('arm_prepare',(0,0,0));s=snapshot();compare('root-seed',s,expected='miss')
    newpt=RAM+0x700000;wr(newpt,rd(m.symbols['pages'],4096));put(m.symbols['g_xpt'],newpt);s=snapshot();compare('root-change',s,expected='miss');compare('root-change-warm',s)
    # Native comparison flags are overwritten; prior exception-status bits are keyed.
    m.call('arm_prepare',(0,0,0));s=snapshot();compare('fpscr-nzcv-seed',s,0,expected='miss');compare('fpscr-nzcv-reuse',s,0xf0000000,expected='hit')
    # Actual live runtime controls: FCW023f, default-NaN + flush-to-zero,
    # cumulative input-denormal/inexact flags, arbitrary overwritten NZCV.
    for fcw in [0x023f,0x027f]:
     for fpscr in [0x03000000,0x03000080,0x03000090,0x63000090,0x0300009f]:
      for top in [0,5]:
       m.call('arm_reset');m.call('arm_prepare',(0,top,0));put(m.context+m.layout['fsw'],(fcw<<16)|0x7800);s=snapshot()
       compare(f'live-fp-{fcw:x}-{fpscr:x}-{top}-cold',s,fpscr,expected='miss')
       compare(f'live-fp-{fcw:x}-{fpscr:x}-{top}-hit',s,fpscr,expected='hit')
    # Tiny normal inputs exercise FZ conversion and cumulative output flags.
    for val in [0x00800000,0x00800001,0x80800000,0x33800000,0x80000000]:
     m.call('arm_reset');m.call('arm_prepare',(0,0,0));put(m.context+m.layout['fsw'],0x023f7800)
     for a in [0x2fc8ac,0x2fc8b0,0x2fc8b4,0x2fc8b8]:put(guest(a),val)
     s=snapshot();compare(f'live-fz-{val:x}-cold',s,0x63000090,expected='miss');compare(f'live-fz-{val:x}-hit',s,0x63000090,expected='hit')
    # Random finite values stress each clamp path and original FISTP/packing.
    rng=random.Random(70110)
    for n in range(60):
     m.call('arm_prepare',(n%4,n%8,0));
     for a in addr:
      if a not in (0x1f0a68,0x1f0a78):put(guest(a),struct.unpack('<I',struct.pack('<f',rng.uniform(-30,30)))[0])
     s=snapshot();compare('random-cold-'+str(n),s,expected='miss');compare('random-hit-'+str(n),s)
    # A caller holding stale roots must execute the original retained block.
    for mode in [1,2,3]:
     m.call('arm_reset');m.call('arm_prepare',(0,0,0));s=snapshot();compare('stale-root-seed-'+str(mode),s,expected='miss')
     put(m.symbols['root_mode'],mode);compare('stale-root-decline-'+str(mode),s,expected='decline')
     put(m.symbols['root_mode'],0);compare('stale-root-recover-'+str(mode),s,expected='miss')
    # Active tracing/watches and uninitialized watch policy decline and invalidate.
    for symbol,value in [('xv_watch_n',1),('xv_watch_n',0xffffffff),('xv_trace_funcs',1)]:
     m.call('arm_reset');m.call('arm_prepare',(0,0,0));s=snapshot();compare(symbol+'-seed',s,expected='miss')
     put(m.symbols[symbol],value);compare(symbol+'-decline-'+str(value),s,expected='decline')
     put(m.symbols[symbol],0);compare(symbol+'-cold-after-disable',s,expected='miss')
    # Runtime disable/re-enable invalidates warmed state. Foreign owner admission
    # changes no owner-local memo or counter; generation changes force a cold miss.
    m.call('arm_reset');m.call('arm_prepare',(0,0,0));s=snapshot();compare('config-seed',s,expected='miss')
    m.call('arm_disable');compare('config-disabled',s,expected='bypass')
    m.call('arm_enable');compare('config-reenabled',s,expected='miss');compare('config-reenabled-hit',s)
    put(m.symbols['owner_allowed'],0);compare('foreign-owner',s,expected='bypass')
    put(m.symbols['owner_allowed'],1);compare('owner-restored',s)
    put(m.symbols['owner_generation'],2);compare('generation-change',s,expected='miss')
    for name,symbol,value in [('arena-trash','arena_bytes',0x2fe000),('image-lo','image_lo',0x300000),('image-hi','image_hi',0x200000)]:
     m.call('arm_reset');m.call('arm_prepare',(0,0,0));s=snapshot();put(m.symbols[symbol],value);compare(name,s,expected='decline')
    m.call('arm_reset')
    result={'comparisons':len(rows),'scope':'standalone original region with retained code_000.o children; no publication sinks or FPS claim','commands':commands,'rows':rows,'oracle_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [B/'recomp/code_011.c',B/'build/recomp/code_000.o',ROOT/'recomp/kernel/xk_model_fog.c']}}
    (O/'prototype-results.json').write_text(json.dumps(result,indent=2)+'\n')
    for r in rows[:8]:print(r['name'],r['outcome'],r['original']['instructions'],r['candidate']['instructions'],r['candidate']['firmware_copy_bytes'])
    print('PASS',len(rows),'full context/8MiB/FPSCR comparisons')


if __name__ == "__main__":
    if not __debug__:raise SystemExit("Run without Python -O")
    main()
