from pathlib import Path
import argparse,os,sys,subprocess,struct,math,json,hashlib
parser=argparse.ArgumentParser(description="Compare generated polygon edge against its original lift on Cortex-A9")
parser.add_argument('--output-dir',type=Path,required=True)
parser.add_argument('--cc',default=str(Path(os.environ.get('VITASDK',str(Path.home()/'vitasdk')))/'bin/arm-vita-eabi-gcc'))
args=parser.parse_args()
s=Path(__file__).resolve().parents[1];d=args.output_dir.resolve();d.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(s/'tools'))
import test_arm_model_palette as base
base.SIZE=2<<20
from test_arm_model_palette import Machine, RAM, PT, STACK, CTX, END, SIZE
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_FPSCR
class SphereMachine(Machine):
    def run(self, function, fixture):
        memory, context, pages, fpscr = fixture
        uc = self.uc
        uc.mem_write(RAM, memory)
        uc.mem_write(CTX, context)
        uc.mem_write(PT, struct.pack('<' + 'I' * len(pages), *pages))
        uc.mem_write(STACK, bytes(65536))
        uc.reg_write(UC_ARM_REG_R0, CTX)
        uc.reg_write(UC_ARM_REG_SP, STACK + 65024)
        uc.reg_write(UC_ARM_REG_LR, END | 1)
        uc.reg_write(UC_ARM_REG_FPSCR, fpscr)
        assert uc.reg_read(UC_ARM_REG_FPSCR) == fpscr, 'ARM model did not retain requested FP controls'
        self.instructions = self.copies = self.copy_bytes = self.yields = 0
        uc.emu_start(self.symbols[function] | 1, END, count=100000)
        assert uc.reg_read(UC_ARM_REG_PC) == END
        return dict(context=bytes(uc.mem_read(CTX, self.layout['size'])),
                    memory=bytes(uc.mem_read(RAM, SIZE)),
                    fpscr=uc.reg_read(UC_ARM_REG_FPSCR),
                    accepted=uc.reg_read(UC_ARM_REG_R0),
                    instructions=self.instructions, copies=self.copies)

(d/'arm-fixture.c').write_text('''#include "xv_x86rt.h"
#include <stddef.h>
#include "kernel/xk_polygon_edge.h"
uint8_t *g_xram,*g_img_base;uint32_t *g_xpt;
const unsigned layout[]={sizeof(xctx),offsetof(xctx,r),offsetof(xctx,st),offsetof(xctx,fsp),offsetof(xctx,fsw),offsetof(xctx,fcw),offsetof(xctx,preempt),offsetof(xctx,f_kind),offsetof(xctx,f_bits),offsetof(xctx,xmm)};
void test_boot(void){xv_native_polygon_edge_init();xv_native_polygon_edge_override(1);}
void test_disable(void){xv_native_polygon_edge_override(-1);}
int sceKernelGetThreadId(void){return 1;}
void abort(void){__builtin_trap();} void xk_os_log(const char *f,...){(void)f;}
char *getenv(const char*n){(void)n;return 0;}int atoi(const char*s){(void)s;return 1;}
void __wrap_xv_preempt(xctx*c){c->preempt=3;}
void *memcpy(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
void *memset(void*d,int s,size_t n){(void)s;(void)n;return d;}
void *memmove(void*d,const void*s,size_t n){(void)s;(void)n;return d;}
''')
cc=args.cc
common=[cc,'-O2','-fno-strict-aliasing','-ffp-contract=off','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-std=gnu11','-I'+str(s/'recomp'),'-ffunction-sections','-fdata-sections','-fstack-usage']
objs=[];commands=[]
for i,p in enumerate([s/'recomp/host/build/original_000B77C0.c',s/'recomp/kernel/xk_polygon_edge.c',d/'arm-fixture.c',s/'recomp/xv_x86rt.c',s/'recomp/kernel/xk_polygon_edge_control.c']):
 o=d/f'arm-{i}.o';cmd=common+['-c',str(p),'-o',str(o)];subprocess.run(cmd,check=True);objs.append(str(o));commands.append(cmd)
elf=d/'arm-test.elf';cmd=common+objs+['-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,--undefined=f_000B77C0,--undefined=xv_math_polygon_edge,--undefined=test_disable,--undefined=layout','-lgcc','-o',str(elf)];subprocess.run(cmd,check=True);commands.append(cmd)
m=SphereMachine(elf);layout=m.layout;rows=[]
empty=(bytes(SIZE),bytes(layout['size']),[i*4096 for i in range(SIZE//4096)],0)
initial=m.run('xv_math_polygon_edge',empty)
assert initial['accepted']==0 and initial['memory']==empty[0] and initial['context']==empty[1] and initial['fpscr']==0
m.run('test_boot',empty)
for mode in range(16):
 for case in range(48):
  memory=bytearray(b'\xa5'*base.SIZE);ctx=bytearray(layout['size']);pages=[(i^1)*4096 for i in range(base.SIZE//4096)];pages[0x48]=pages[0x18];pages[0x49]=pages[0x19]
  def put(a,b):
   for k,v in enumerate(b):memory[pages[(a+k)>>12]+((a+k)&4095)]=v
  def word(a,v):put(a,struct.pack('<I',v))
  def fp(a,v):put(a,struct.pack('<f',v))
  def field(n,v,fmt='I'):struct.pack_into('<'+fmt,ctx,layout[n],v)
  def reg(i,v):struct.pack_into('<I',ctx,layout['r']+i*4,v)
  for i in range(8):reg(i,0x98765432+i);struct.pack_into('<d',ctx,layout['st']+i*8,i+.375)
  field('fsp',case%8);field('fsw',0xabcd,'H');field('fcw',0x37f,'H');field('preempt',1 if case%3==0 else 1000);field('f_kind',2);field('f_bits',32)
  sp=0x61ff0+case%4;poly=0x18ff0+case%8;point=0x28ffc+case%8;count=3+case%14
  if case%19==0:point=poly
  if case%23==0:point=0x48ff0+case%8
  if case%31==0:poly=sp+8
  reg(4,sp);reg(1,poly)
  for j in range(count):fp(poly+8*j,math.cos(6.283185307*j/count));fp(poly+8*j+4,math.sin(6.283185307*j/count))
  fp(point,case%7*.4-1.2);fp(point+4,case%5*.3-.6)
  if case>=32:
   edges=[0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc01234,0x7f801234]
   word(poly+4,edges[(case-32)%len(edges)])
  struct.pack_into('<f',memory,0x1f0a68,0);fp(0x1f0a68,0);word(sp,0x12345678);word(sp+4,0 if case%29==0 else 0xffff if case%37==0 else count);word(sp+8,point);fp(sp+12,case%9*.2)
  fixture=(bytes(memory),bytes(ctx),pages,(mode<<22)|(0x9f if case&1 else 0))
  a=m.run('f_000B77C0',fixture);b=m.run('xv_math_polygon_edge',fixture)
  for name in ('context','memory','fpscr'):
   if a[name]!=b[name]:
    if name!='fpscr':(d/f'mismatch-{mode}-{case}-{name}.expected').write_bytes(a[name]);(d/f'mismatch-{mode}-{case}-{name}.actual').write_bytes(b[name])
    raise AssertionError((mode,case,name,a['fpscr'],b['fpscr']))
  assert b['accepted']==1
  rows.append({'mode':mode,'case':case,'original':a['instructions'],'candidate':b['instructions'],'copies':(a['copies'],b['copies'])})
 print('PASS mode',mode,flush=True)
m.run('test_disable',fixture)
disabled=m.run('xv_math_polygon_edge',fixture)
assert disabled['accepted']==0 and disabled['memory']==fixture[0] and disabled['context']==fixture[1] and disabled['fpscr']==fixture[3]
inputs=[s/'recomp/host/build/original_000B77C0.c',s/'recomp/kernel/xk_polygon_edge.c',s/'recomp/kernel/xk_polygon_edge_control.c',s/'recomp/kernel/xk_polygon_edge.h',s/'recomp/xv_x86rt.c',s/'recomp/xv_x86rt.h',s/'tools/test_arm_polygon_edge.py',s/'tools/test_arm_model_palette.py',d/'arm-fixture.c']
report={'fixtures':len(rows),'default_off_and_restore_minus_one':True,'input_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs},'full_context_memory_fp_status_match':True,'rows':rows,'commands':commands,'elf_sha256':hashlib.sha256(elf.read_bytes()).hexdigest(),'limitation':'Instruction counts exclude modeled memory copies; no hardware cycles or FPS claim.'}
(d/'arm-result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(rows[:4]),flush=True)
