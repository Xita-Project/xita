from pathlib import Path
import sys,struct,json
S=Path(__file__).resolve().parents[1];sys.path.insert(0,str(S/'tools'))
from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_FPSCR

def run(out):
 P=Path(out).resolve()
 rows=[]
 class Lane:
  def __init__(self,candidate):
   self.candidate=candidate;self.m=RuntimeMachine(P/'caller.elf',True);self.fp=0x63000090;self.front=[]
   self.pubsize=self.word(self.m.symbols['publication_size'])
   self.m.uc.hook_add(UC_HOOK_CODE,self.hook)
  def word(self,a):return struct.unpack('<I',bytes(self.m.uc.mem_read(a,4)))[0]
  def put(self,a,v):self.m.uc.mem_write(a,struct.pack('<I',v))
  def snap(self):return tuple(bytes(self.m.uc.mem_read(a,n))for a,n in [(self.m.context,self.m.layout['size']),(RAM,SIZE),(self.m.symbols['xd3d_state'],self.pubsize)])
  def hook(self,uc,address,size,user):
   if address==self.m.symbols['uv_frontier']&~1:self.front.append((self.snap(),uc.reg_read(UC_ARM_REG_FPSCR)))
  def call(self,n,args=()):return self.m.call(n,args,fpscr=self.fp)
  def invoke(self,route,mode):
   self.put(self.m.symbols['callback_mode'],mode);self.front=[]
   vals=lambda:struct.unpack('<22I',bytes(self.m.uc.mem_read(self.m.symbols['counts'],88)))
   pre=vals();row=self.call('arm_'+route+('_candidate'if self.candidate else'_original'));post=vals();self.fp=row['fpscr']
   return row,[sum(post[3:5])-sum(pre[3:5]),post[5]-pre[5],sum(post[13:19])-sum(pre[13:19])]
 for route,back in [('front',0),('back',1)]:
  for mode in [4,5,6,7,8]:
   a,b=Lane(False),Lane(True)
   for lane in [a,b]:lane.call('arm_caller_prepare',(back,0,0));lane.call('arm_reset')
   for step in range(2):
    old,_=a.invoke(route,mode if step==0 else 0);new,delta=b.invoke(route,mode if step==0 else 0)
    assert a.snap()==b.snap() and a.front==b.front and a.fp==b.fp,(route,mode,step,'state')
    expected=[1,0,0]if step and mode==4 else[0,1,0]
    assert delta==expected,(route,mode,step,delta)
    rows.append(dict(route=route,callback=mode,step=step,original=old,candidate=new,counters=delta))
    if not step:
     # Callback already performed the frontier mutation. End any surviving outer
     # model, then make the next original caller's setup and selected scope.
     for lane in [a,b]:
      lane.call('arm_scope_end');lane.call('arm_caller_prepare',(back,0,0));lane.call('arm_scope_begin')
 (P/'callback-results.json').write_text(json.dumps({'comparisons':len(rows),'rows':rows},indent=2)+'\n');print('PASS',len(rows))

