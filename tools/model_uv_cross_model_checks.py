from pathlib import Path
import sys,struct,json
S=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(S/'tools'))
from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE

def run(out):
 P=Path(out).resolve()
 rows=[]
 class Case:
  def __init__(self,name):
   self.name=name;self.m=RuntimeMachine(P/'uv.elf',True);self.fp=0x63000090
   self.m.call('arm_prepare',(0,0,0));self.m.call('arm_reset');self.initial=self.snap()
  def rd(self,a,n):return bytes(self.m.uc.mem_read(a,n))
  def wr(self,a,b):self.m.uc.mem_write(a,b)
  def word(self,a):return struct.unpack('<I',self.rd(a,4))[0]
  def put(self,a,v):self.wr(a,struct.pack('<I',v))
  def sym(self,n):return self.m.symbols[n]
  def guest(self,a):return RAM+self.word(self.word(self.sym('g_xpt'))+(a>>12)*4)+(a&4095)
  def snap(self):return self.rd(self.m.context,self.m.layout['size']),self.rd(RAM,SIZE)
  def restore(self,s):self.wr(self.m.context,s[0]);self.wr(RAM,s[1])
  def call(self,n,args=()):return self.m.call(n,args,fpscr=self.fp)
  def counts(self):
   v=struct.unpack('<22I',self.rd(self.sym('counts'),88));return [sum(v[3:5]),v[5],sum(v[13:19])]
  def compare(self,tag,expected):
   s=self.snap();before=self.counts();old=self.call('arm_original');ref=self.snap();self.restore(s);new=self.call('arm_candidate');out=self.snap();after=self.counts();delta=[b-a for a,b in zip(before,after)]
   assert ref==out and old['fpscr']==new['fpscr'],(self.name,tag,'state')
   assert delta=={'hit':[1,0,0],'cold':[0,1,0],'decline':[0,0,1],'foreign':[0,0,0]}[expected],(self.name,tag,delta)
   rows.append(dict(case=self.name,step=tag,expected=expected,original=old,candidate=new));self.fp=old['fpscr'];return s
  def seed(self):self.compare('seed','cold');self.restore(self.initial)
  def next(self,packet=0x111000,sp=0x91800,material=0x101100,scalar=0xfffffff0,null=False):
   self.call('arm_scope_end')
   old=self.initial
   self.wr(self.guest(material),old[1][self.guest(0x100100)-RAM:self.guest(0x100100)-RAM+56])
   self.wr(self.guest(sp),old[1][self.guest(0x90800)-RAM:self.guest(0x90800)-RAM+28])
   for r,v in [(4,sp),(6,material),(3,sp+0x80),(7,sp+0x90),(1,0 if null else packet+0x84)]:self.put(self.m.context+r*4,v)
   self.put(self.guest(packet+0x88),scalar);self.put(RAM+(4<<20)+0x2e3520,packet)
   self.call('arm_scope_begin')
 # Different packet/material/output/stack and null/different otherwise unread scalar pointer.
 for null in [False,True]:
  c=Case('relocated-null-'+str(null));c.seed();c.next(null=null)
  # Old input/output/scratch and scalar storage are now unrelated, poisoned bytes.
  for a,n in [(0x907e0,0xc0),(0x100100,56),(0x110084,8),(0x120000,32)]:c.wr(c.guest(a),b'\xe7'*n)
  c.compare('new-model','hit')
  assert c.word(c.sym('cross_counts')+4)==1
 # Inter-model changed effective input misses, then becomes reusable; preserve original control semantics.
 for i in range(6):
  c=Case('argument-'+str(i));c.seed();c.next();c.put(c.guest(0x91804+i*4),[0x3fc00000,0x3f000000,0x3e800000,0xbe800000,0x42340000,0x41900000][i]);s=c.compare('changed','cold');c.restore(s);c.compare('repeat','hit')
 for what in ['sticky','fcw','fpscr','descriptor','constant','alias','packet-inside','roots-inside']:
  c=Case(what);c.seed();c.next();expected='cold'
  if what=='sticky':c.put(c.m.context+c.m.layout['fsw'],0x023f7880)
  if what=='fcw':c.put(c.m.context+c.m.layout['fsw'],0x027f7800)
  if what=='fpscr':c.fp=0x63000080
  if what=='descriptor':c.put(c.guest(0x101100+44),0);expected='decline'
  if what=='constant':c.put(c.guest(0x1f0b40),0x3c8efa36);expected='decline'
  if what=='alias':c.put(c.m.context+3*4,0x91804);expected='decline'
  if what=='packet-inside':c.put(RAM+(4<<20)+0x2e3520,0x112000);expected='decline'
  if what=='roots-inside':c.put(c.sym('root_mode'),1);expected='decline'
  c.compare('changed',expected)
 for what in ['present-between','scene-between','present-open','nested','exit-root','exit-diagnostic','pending-present','inactive-exit']:
  c=Case(what);c.seed()
  if what in ['present-between','scene-between']:
   c.call('arm_scope_end')
   if what=='present-between':c.call('arm_present')
   else:c.call('arm_scene_end');c.call('arm_scene_begin')
   c.next();c.compare('new-model','cold')
  elif what=='present-open':
   c.call('arm_present');c.compare('blocked','decline');c.restore(c.initial);c.next();c.compare('new-model','cold')
  elif what=='nested':
   c.call('arm_nested_begin');c.call('arm_nested_end');c.next();c.compare('new-model','cold')
  elif what=='exit-root':
   c.put(RAM+(4<<20)+0x2e3520,0x112000);c.next();c.compare('new-model','cold')
  elif what=='exit-diagnostic':
   c.put(c.sym('xv_watch_n'),1);c.call('arm_scope_end');c.put(c.sym('xv_watch_n'),0);c.next();c.compare('new-model','cold')
  elif what=='pending-present':
   c.put(c.guest(0x90804),0x3fc00000);c.call('arm_pending_begin');c.call('arm_present');c.call('arm_pending_finish');c.restore(c.initial);c.next();c.compare('new-model','cold')
  else:
   c.call('arm_scene_end');c.call('arm_scope_end');c.call('arm_scene_begin');c.next();c.compare('new-model','cold')
 for kind in [1,2,3,4,5,7]:
  c=Case('foreign-'+str(kind));c.seed();c.call('arm_owner_case',(kind,));c.call('arm_scope_end');c.compare('foreign-call','foreign')
 # Naturally chained floating state; only a real caller's argument/register setup rebuilt.
 c=Case('natural-state-chain');c.compare('first','cold')
 for i in range(1,6):c.next(packet=0x111000+(i%2)*0x1000,sp=0x91800+(i%2)*0x1000);c.compare('model-'+str(i),'hit')
 # Joined reporting clears integer counts without altering the valid payload.
 c=Case('joined-report');c.seed();c.next();state=c.compare('cross-hit','hit')
 c.restore(state);c.compare('same-model-hit','hit')
 assert c.word(c.sym('cross_counts'))==1 and c.word(c.sym('cross_counts')+4)==1
 assert c.word(c.sym('cross_counts')+8)>=2
 c.call('xk_model_uv_report',(60,))
 assert c.rd(c.sym('counts'),88)==bytes(88) and c.rd(c.sym('cross_counts'),12)==bytes(12)
 c.restore(state);c.compare('after-report','hit')
 assert c.word(c.sym('cross_counts')+4)==0
 # Measure clean exit and next begin separately, not just helper hit. OS stub costs remain unmodeled.
 c=Case('cost');c.seed();cost={}
 for name in ['arm_scope_end','arm_scope_begin','arm_present']:
  row=c.call(name);row['thread_calls']=c.m.by_address[c.sym('__wrap_sceKernelGetThreadId')&~1];row['fiber_calls']=c.m.by_address[c.sym('xk_os_fiber_current')&~1];cost[name]=row
 result={'comparisons':len(rows),'rows':rows,'lifecycle_cost':cost}
 (P/'cross-results.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',len(rows),'cross-model state/lifecycle/counter comparisons')

