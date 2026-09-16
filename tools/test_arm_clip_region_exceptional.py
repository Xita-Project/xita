"""Private multi-pass exceptional-value matrix; no normalization of results."""
from pathlib import Path
import struct,json,sys,itertools
S=Path(__file__).resolve().parents[1]
if '--reuse' not in sys.argv:sys.argv.append('--reuse')
source=(S/'tools/test_arm_clip_region.py').read_text();scope={'__file__':str(S/'tools/test_arm_clip_region.py')}
exec(compile(source[:source.index("# ARM_BUILD")],str(S/'tools/test_arm_clip_region.py'),'exec'),scope)
globals().update({k:v for k,v in scope.items() if k not in ['__file__']})
m=FusionMachine(O/'arm-test.elf');layout=m.layout;m.uc.mem_write(m.symbols['initialized'],struct.pack('<I',1))
m.run('test_boot',(bytes(SIZE),bytes(layout['size']),[i*4096 for i in range(512)],0))
values=[0,0x80000000,1,0x807fffff,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0xffc05678,0x7f801234,0xff805678,0x7fffffff,0xffffffff]
def make_fixture(case,mode):
 memory=bytearray(b'\xa5'*SIZE);context=bytearray(layout['size']);pages=[(i^1)*4096 for i in range(SIZE//4096)]
 def word(a,v):
  for j,b in enumerate(struct.pack('<I',v)):memory[pages[(a+j)>>12]+((a+j)&4095)]=b
 def fp(a,v):word(a,struct.unpack('<I',struct.pack('<f',v))[0])
 poly=0x18ff0;clip=0x38ff0;out=0x58ff0;sp=0x91ff0
 for i,(x,y) in enumerate([(-2.,-2.),(2.,-2.),(2.,2.),(-2.,2.)]):fp(poly+i*8,x);fp(poly+i*8+4,y)
 for i,(x,y) in enumerate([(1.,0.),(0.,1.),(-1.,0.),(0.,-1.)]):fp(clip+i*8,x);fp(clip+i*8+4,y)
 pair,shape=divmod(case,8);a=values[pair%16];b=values[(pair//16)%16]
 locations=[(poly,poly+4),(poly+4,poly+12),(poly+4,poly+16),(clip,clip+4),(clip+4,clip+12),(poly+4,clip+4),(poly,clip+12),(poly+12,clip+16)][shape]
 word(locations[0],a);word(locations[1],b)
 fp(0x1F0A68,0);fp(0x1F0A78,1)
 for j,bval in enumerate(struct.pack('<d',9.999999747378752e-05)):memory[pages[(0x1f0af8+j)>>12]+((0x1f0af8+j)&4095)]=bval
 for j,v in enumerate((0x12345678,4,clip,32,out,0)):word(sp+j*4,v)
 for j in range(8):struct.pack_into('<I',context,layout['r']+j*4,0x87654321+j);struct.pack_into('<Q',context,layout['st']+j*8,[0x7ff8000000001234,0xfff0000000004321,0x8000000000000000,1][j%4])
 for n,v in [(4,sp),(1,4),(2,poly)]:struct.pack_into('<I',context,layout['r']+n*4,v)
 for n,v in [('fsp',case%8),('f_kind',2),('f_bits',32),('preempt',1)]:struct.pack_into('<I',context,layout[n],v)
 struct.pack_into('<H',context,layout['fsw'],0xabcd);struct.pack_into('<H',context,layout['fcw'],0x37f)
 return (bytes(memory),bytes(context),pages,mode<<22),dict(a=hex(a),b=hex(b),shape=shape)
rows=[];failures=[]
cases=[int(sys.argv[sys.argv.index('--case')+1])] if '--case' in sys.argv else range(int(sys.argv[sys.argv.index("--start")+1]) if "--start" in sys.argv else 0,2048)
modes=[int(sys.argv[sys.argv.index('--mode')+1])] if '--mode' in sys.argv else [0,4,8,12]
for mode in modes:
 for case in cases:
  fixture,meta=make_fixture(case,mode)
  result=[m.run(fn,fixture) for fn in ('run_original','run_current','run_fused')]
  keys=('context','memory','pages','fpscr','events','clip_count','probe_count','yields')
  comparisons={name:[k for k in keys if result[a][k]!=result[b][k]] for name,a,b in [('original_current',0,1),('original_fused',0,2),('current_fused',1,2)]}
  row=dict(case=case,mode=mode,**meta,fields=comparisons)
  if any(comparisons.values()):
   failures.append(row);print('MISMATCH',row,flush=True)
   if len(failures)<16:
    for index,res in enumerate(result):
     for key in ['context','memory']:(O/f'exception-{mode}-{case}-{index}-{key}.bin').write_bytes(res[key])
  rows.append(row)
  if case%128==0:print('PROGRESS',mode,case,'failures',len(failures),flush=True)
  if '--first' in sys.argv and failures:break
 if '--first' in sys.argv and failures:break
(O/'exceptional-result.json').write_text(json.dumps(dict(rows=rows,failures=failures),indent=2)+'\n');print('COMPLETE',len(rows),'failures',len(failures))

assert not failures, "strict ARM exceptional mismatches"
