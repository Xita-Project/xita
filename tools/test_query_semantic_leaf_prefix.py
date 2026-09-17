#!/usr/bin/env python3
"""Exceptional vertex data through actual retained objects to a real preempt.

The original query can fault later on nonfinite geometry. Stop before executing
the first original preempt callback after its first nonfinite distance store.
Compare the fully published context and all guest state there, without changing
the guest budget, data, continuation or instructions during execution.
"""
import argparse,hashlib,json,struct,sys,subprocess,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT)]
from test_arm_cluster_runtime import RuntimeMachine,RAM,SIZE,STACK,END
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC,UC_ARM_REG_FPSCR

def sha(data):return hashlib.sha256(data).hexdigest()
class Machine(RuntimeMachine):
    def word(self,a):return struct.unpack('<I',self.uc.mem_read(a,4))[0]
    def gptr(self,a):return self.word(self.symbols['g_xram'])+self.word(self.word(self.symbols['g_xpt'])+4*(a>>12))+(a&4095)
    def put(self,a,w):self.uc.mem_write(self.gptr(a),struct.pack('<I',w))
    def step(self,uc,address,size,user):
        if self.active and address in self.semantic_entries:self.semantic_calls+=1
        if self.active and self.distance is not None and address==self.preempt:
            assert uc.reg_read(UC_ARM_REG_R0)==self.context
            assert self.word(self.symbols['ct_current_context'])==self.context
            self.stopped=address;uc.emu_stop();return
        super().step(uc,address,size,user)
    def write(self,uc,access,address,size,value,user):
        if not self.active or not(RAM<=address and address+size<=RAM+SIZE):return
        value&=(1<<(8*size))-1
        self.writes.append((address-RAM,size,value))
        # Other fixture words/return addresses do not use these IEEE patterns.
        # Verify the same physical address, value and write prefix across lanes.
        if self.distance is None and size==4 and value in self.expected_distance_words:
            self.distance=dict(physical_address=address-RAM,value=value,arm_store_pc=uc.reg_read(UC_ARM_REG_PC),write_index=len(self.writes)-1)
    def prefix(self,fpscr,expected):
        self.active=True;self.distance=None;self.stopped=None;self.writes=[];self.semantic_calls=0
        self.expected_distance_words=expected
        u=self.uc;u.reg_write(UC_ARM_REG_SP,STACK+65024);u.reg_write(UC_ARM_REG_LR,END|1);u.reg_write(UC_ARM_REG_FPSCR,fpscr)
        self.instructions=self.copies=self.copy_bytes=self.yields=0
        name='arm_original' if self.lane=='reference' else 'arm_candidate'
        u.emu_start(self.symbols[name]|1,END,count=1000000)
        self.active=False
        assert self.distance is not None and self.stopped==self.preempt,(self.lane,self.distance,self.stopped,hex(u.reg_read(UC_ARM_REG_PC)))
        root=self.word(self.symbols['g_xpt'])
        snapshot=dict(context=bytes(u.mem_read(self.context,self.layout['size'])),memory=bytes(u.mem_read(RAM,SIZE)),
            original_pages=bytes(u.mem_read(self.symbols['ct_original_pages'],4096)),
            alternate_pages=bytes(u.mem_read(self.symbols['ct_alternate_pages'],4096)),
            root='original' if root==self.symbols['ct_original_pages'] else 'alternate' if root==self.symbols['ct_alternate_pages'] else root,
            arena=self.word(self.symbols['g_xram']),image=self.word(self.symbols['g_img_base']),fpscr=u.reg_read(UC_ARM_REG_FPSCR),
            writes=self.writes,distance={k:v for k,v in self.distance.items() if k!='arm_store_pc'})
        for name in ('ct_yields','ct_events','ct_seen','ct_boundary','ct_collect_calls'):
            snapshot[name]=self.word(self.symbols[name])
        return snapshot

def main():
    if not __debug__:raise SystemExit('Refusing optimized Python')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cost-dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out=a.out.resolve()
    if a.out.is_relative_to(ROOT):p.error('private output must remain outside worktree')
    a.out.mkdir(parents=True,exist_ok=False)
    machines={}
    for lane in ('reference','baseline','inline'):
        m=Machine(a.cost_dir/(lane+'.elf'));m.lane=lane;m.active=False
        m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
        m.preempt=m.symbols['__wrap_xv_preempt']&~1
        m.semantic_entries=set()
        if lane=='inline':
            disassembly=subprocess.check_output(['/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-objdump','-d',str(a.cost_dir/(lane+'.elf'))],text=True)
            instructions=[]
            for line in disassembly.splitlines():
                match=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{4,8}\s+)+(\S+)',line)
                if match:instructions.append((int(match[1],16),match[2]))
            pattern=['vsub.f32']*4+['vmul.f32']*4+['vadd.f32']*2
            m.semantic_entries={instructions[i][0] for i in range(len(instructions)-9) if [op for _,op in instructions[i:i+10]]==pattern}
            assert m.semantic_entries
        m.uc.hook_add(UC_HOOK_MEM_WRITE,m.write)
        machines[lane]=m
    patterns=[('infinity',[0x7f800000,0xff800000,0x7f800000]),
              ('one-quiet-nan',[0x7fc12345,0xbe800000,0x3e800000]),
              ('distinct-quiet-nans',[0x7fc12345,0x7fc23456,0x7fc34567]),
              ('one-signaling-nan',[0x7f812345,0xbe800000,0x3e800000]),
              ('distinct-signaling-nans',[0x7f812345,0x7f823456,0x7f834567]),
              ('mixed-sign-payloads',[0xffc12345,0x7f823456,0xffc34567])]
    rows=[]
    for name,words in patterns:
        for fpscr in (0,0x400000,0x800000,0xc00000,0x0300009f):
            expected=None;result={}
            possible={0x7f800000,0x7fc00000}|{w|0x400000 for w in words if w&0x7f800000==0x7f800000}
            for lane,m in machines.items():
                m.call('arm_prepare',(1,0,1))
                for vertex in range(4):
                    for axis,word in enumerate(words):m.put(0x1a000+16*vertex+axis*4,word)
                actual=m.prefix(fpscr,possible)
                if lane=='inline':assert m.semantic_calls>0,'semantic interval not reached'
                if expected is None:expected=actual
                checks={key:actual[key]==value for key,value in expected.items()}
                result[lane]=dict(checks=checks,context=actual['context'].hex(),memory_sha256=sha(actual['memory']),
                    ordered_writes_sha256=sha(json.dumps(actual['writes']).encode()),guest_write_events=len(actual['writes']),
                    fpscr=actual['fpscr'],semantic_distance_calls=m.semantic_calls,distance=m.distance,callback_pc=m.stopped,context_pointer=m.context)
                if not all(checks.values()):
                    (a.out/'failure.json').write_text(json.dumps(dict(name=name,fpscr=fpscr,lanes=result),indent=2)+'\n')
                    raise AssertionError((name,fpscr,lane,checks))
            rows.append(dict(name=name,input_words=words,fpscr=fpscr,lanes=result))
            (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
            print('PASS retained preempt prefix',name,hex(fpscr),flush=True)
    (a.out/'receipt.json').write_text(json.dumps(dict(cases=len(rows),
        original_objects_executed=True,stopped_at_existing_preempt=True,no_runtime_mutation=True,
        full_context_memory_maps_roots_fpscr_ordered_writes=True,
        limitations=['This is an exact exceptional prefix, not a completed nonfinite-geometry query.'],
        inputs={str(a.cost_dir/(lane+'.elf')):sha((a.cost_dir/(lane+'.elf')).read_bytes()) for lane in machines}),indent=2)+'\n')

if __name__=='__main__':main()
