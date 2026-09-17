#!/usr/bin/env python3
"""Focused qualification against actual retained whole-query ARM objects.

Adversarial states are introduced only after the fixture's existing, actual
87F8D preempt callback returns. No new guest/callback entry or budget decrement
is inserted. These are explicit callback-mutation fixtures, not production
scheduler/abort tests or cost measurements. Observe complete state before and
after callbacks and the actual scalar map/entry/count read sequence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT)]
from prototype_query_ancestor_scalar import Machine as Base, record_write, RAM, SIZE, sha
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

PINNED_ELFS={'reference': 'e68ef4e220efeb8a12b5ae4e03381344e7b29c7aefd1d570665262749fd7e468', 'baseline': '51eece5a8217ae24f9c53fd362501830a446197acd43db15e4218eba9111b6e2', 'inline': 'aacbca377e974da0d10ec970111519ae936f6fb3cddb101c886402935c2ac4c7'}

def symbol(path,name):
    with path.open('rb') as f:
        return next(s['st_value']&~1 for s in ELFFile(f).get_section_by_name('.symtab').iter_symbols() if s.name==name)

def negative_controls(cost_dir,out):
    """Execute deliberately wrong compiled scalar bodies; require rejection.

    These reuse ordinary whole-query budget=1 inputs. No injected callback,
    arbitrary PC entry, skipped guest work or reference repair is involved.
    """
    source=(cost_dir/'inline.c').read_text()
    compile_record=json.loads((cost_dir/'build.json').read_text())['commands'][1]
    link=json.loads((cost_dir/'execution-commands.json').read_text())[3]
    wrong={
        'drop-raw-carry':('nq_ancestor_carry=nq_ancestor_value<q1;','nq_ancestor_carry=0;'),
        'double-backedge-budget':('if(--nq_ancestor_budget<=0)','if((nq_ancestor_budget-=2)<=0)')}
    paths={'reference':cost_dir/'reference.elf'};commands=[]
    for name,(before,after) in wrong.items():
        if source.count(before)!=1:raise AssertionError('negative-control target drift')
        path=out/(name+'.c');path.write_text(source.replace(before,after))
        command=list(compile_record['command']);command[command.index('-c')+1]=str(path);command[command.index('-o')+1]=str(out/(name+'.o'))
        subprocess.run(command,cwd=compile_record['cwd'],check=True);commands.append(dict(command=command,cwd=compile_record['cwd']))
        command=list(link);old=str(cost_dir/'inline.o');assert command.count(old)==1
        command[command.index(old)]=str(out/(name+'.o'));command[-1]=str(out/(name+'.elf'))
        subprocess.run(command,check=True);commands.append(dict(command=command));paths[name]=Path(command[-1])
    rows=[];expected=None
    for name,path in paths.items():
        m=Base(path,False);m.active=False;m.observers={m.symbols[n]&~1:n for n in ('__wrap_xv_preempt','ct_observe_entry')}
        m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
        m.uc.hook_add(UC_HOOK_MEM_WRITE,record_write,m)
        m.call('arm_prepare',(2,0,1));stats=m.call('arm_original' if name=='reference' else 'arm_candidate')
        state=m.snapshot();observations=m.records
        if expected is None:expected=state;oracle=observations;fp=stats['fpscr'];writes=m.writes;continue
        checks={k:state[k]==v for k,v in expected.items()}
        checks.update(observers=observations==oracle,fpscr=stats['fpscr']==fp,ordered_guest_writes=m.writes==writes)
        if all(checks.values()):raise AssertionError('negative control was not rejected: '+name)
        first=next((i for i,(actual,wanted) in enumerate(zip(observations,oracle)) if actual!=wanted),None)
        changed=[]
        if first is not None:
            actual=bytes.fromhex(observations[first]['context']);wanted=bytes.fromhex(oracle[first]['context'])
            changed=[i for i,(x,y) in enumerate(zip(actual,wanted)) if x!=y]
        rows.append(dict(name=name,rejected=True,checks=checks,first_observer_difference=first,context_byte_differences=changed,
            oracle_observers=len(oracle),candidate_observers=len(observations),elf_sha256=sha(path.read_bytes())))
        print('REJECTED negative control',name,'context bytes',changed,flush=True)
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    (out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')

class Machine(Base):
    def put(self,a,v):self.uc.mem_write(a,struct.pack('<I',v&0xffffffff))
    def root(self):return self.word(self.symbols['g_xpt'])
    def addr(self,a,root=None):
        a&=0xffffffff
        return self.word(self.symbols['g_xram'])+self.word((root or self.root())+4*(a>>12))+(a&4095)
    def guest_word(self,a,v,root=None):self.put(self.addr(a,root),v)
    def snapshot(self,hashed=False):
        out=super().snapshot(hashed)
        if hashed:
            for k in ('original_pages','alternate_pages'):out[k]=sha(bytes.fromhex(out[k]))
        return out
    def mutate(self):
        case=self.case;regs=list(struct.unpack('<8I',self.uc.mem_read(self.context,32)))
        old_query,sp,original_target=regs[6],regs[4],regs[1]
        # The first actual ancestor miss uses a deliberately invalid fixture
        # plane ID. A returning callback may change ECX; choose an existing
        # real plane so forced-match paths complete the original projection.
        target=original_target&0x80000001
        query=case.get('query',0x22000);root=self.root()
        if query!=old_query:
            data=bytearray()
            for offset in range(0x900):data+=self.uc.mem_read(self.addr(old_query+offset),1)
            for offset in range(0x900):self.uc.mem_write(self.addr(query+offset),bytes(data[offset:offset+1]))
        if case.get('mapping') in ('same-table','different-root'):
            replacement=self.symbols['ct_alternate_pages']
            if case['mapping']=='different-root':
                self.uc.mem_write(replacement,bytes(self.uc.mem_read(root,4096)))
                self.put(self.symbols['g_xpt'],replacement)
                alternate=0x310000
                self.uc.mem_write(RAM+alternate,bytes(self.uc.mem_read(self.addr(query,root)&~4095,4096)))
                self.put(replacement+4*(query>>12),alternate)
            physical=0x300000
            self.uc.mem_write(RAM+physical,bytes(self.uc.mem_read(self.addr(query,root)&~4095,4096)))
            self.put(root+4*(query>>12),physical)
        if case.get('alias')=='entry-count':
            assert query==0x22000
            self.put(root+4*0x23,self.word(root+4*0x22))
        count=case.get('count',3);index=case.get('index',0);counter=case.get('counter',index)
        if case.get('alias')=='stack':
            # The entry aliases actual ESP+0x14 projection scratch. Preserve
            # the real frame's saved leaf pointer/return words and descriptor.
            index=((sp+0x14)-(query+0x1c))//4
        fill=min(count,256) if count<0x80000000 else 1
        for i in range(fill):self.guest_word(query+0x1c+4*i,target^1,root)
        where=case.get('match','first')
        if where=='first':match=index
        elif where=='middle':match=count//2
        elif where=='last':match=count-1
        elif where=='next-wrapped':match=(counter+1)&0xffff;match=match if match<0x8000 else match-0x10000
        else:match=None
        if match is not None:self.guest_word(query+0x1c+4*match,target,root)
        if case.get('force_initial_miss'):
            self.guest_word(query+0x1c+4*index,target^1,root)
        self.guest_word(query+0x18,count,root)
        regs[0]=index&0xffffffff;regs[1]=target;regs[2]=counter&0xffffffff;regs[6]=query
        self.uc.mem_write(self.context,struct.pack('<8I',*regs))
        flags=[5,0xf1f2f3f4,0x91929394,0x41424344,16,0xa5a5a5a5,0x5758595a,0x5a5a5a5a,0xc1c2c3c4]
        self.uc.mem_write(self.context+40,struct.pack('<9I',*flags))
        self.put(self.context+self.layout['preempt'],case.get('budget',100000))
        self.mutations+=1;self.watching=True
        self.mutation_summary=dict(query=hex(query),original_query=hex(old_query),sp=hex(sp),target=target,
            count=count,index=hex(index&0xffffffff),counter=hex(counter&0xffffffff),captured_root='original')
    def step(self,u,a,n,o):
        if self.active and self.pending==a:
            self.pending=None
            if self.do_mutate:self.mutate();self.do_mutate=False
            post=self.snapshot(True);post.update(name='callback-return',fpscr=u.reg_read(UC_ARM_REG_FPSCR),tag=0)
            self.records.append(post)
        if self.active and self.observers.get(a)=='__wrap_xv_preempt':
            lr=u.reg_read(UC_ARM_REG_LR)&~1
            if self.case.get('mutation') and not self.mutations and lr==self.edge_return:
                # Verify the actual real scalar-loop state, rather than using
                # only a hardcoded ARM address to inject a made-up entry.
                c=struct.unpack('<19I',u.mem_read(self.context,76));r=c[:8]
                if not (self.word(self.addr(r[6]))==0x11000 and c[10]==3 and c[11]==r[0]
                        and c[12]==self.word(self.addr(r[6]+0x18)) and c[14]==32):
                    raise AssertionError('not the source-reviewed 87F8D frontier')
                self.do_mutate=True
            self.pending=lr
        if self.active and self.watching and a in self.scan_exits:
            self.scan_exit=self.scan_exits[a];self.watching=False
        if self.active and self.watching and a in self.read_pcs:
            kind,base,index,scale=self.read_pcs[a]
            address=(u.reg_read(base)+u.reg_read(index)*scale)&0xffffffff
            if 'map' in kind:
                table=next((name for name in ('ct_original_pages','ct_alternate_pages') if self.symbols[name]<=address<self.symbols[name]+4096),None)
                if not table:raise AssertionError('unreviewed captured table')
                location=(table,(address-self.symbols[table])//4)
            else:location=address-RAM
            self.reads.append((kind,location,4,self.word(address)))
        super().step(u,a,n,o)
    def call(self,name,*args,**kwargs):
        self.pending=None;self.do_mutate=False;self.mutations=0;self.watching=False;self.reads=[];self.scan_exit=None;self.mutation_summary=None
        return super().call(name,*args,**kwargs)

def cases():
    out=[]
    for count in (0,1,3,4,5,255,256):
        for match in (('miss',) if count==0 else ('first','middle','last','miss')):
            out.append(dict(name=f'count{count}-{match}',mutation=True,count=count,match=match))
    for count,match,budget in [(3,'first',0),(3,'first',1),(3,'middle',1),(3,'last',1),(3,'last',2),
            (3,'last',3),(3,'miss',1),(3,'miss',2),(3,'miss',3),(5,'last',3),(5,'last',4),
            (5,'miss',4),(5,'miss',5),(4,'middle',0),(4,'last',-1)]:
        out.append(dict(name=f'budget{budget}-count{count}-{match}',mutation=True,count=count,match=match,budget=budget))
    for name,index,counter,match in [
            ('noncanonical-index',0x10000,0xabcdef00,'first'),
            ('raw-high-counter',0,0xdead0000,'last'),
            ('counter-uint-wrap',0xffffffff,0xffffffff,'next-wrapped'),
            ('counter-sign-wrap',0xffffffff,0x7fffffff,'next-wrapped'),
            ('index-int16-wrap',0x7fff,0xffff7fff,'next-wrapped')]:
        out.append(dict(name=name,mutation=True,count=3,index=index,counter=counter,match=match,budget=1,
                        force_initial_miss=name=='index-int16-wrap'))
    for count in (0x80000000,0xffffffff):
        out.append(dict(name=f'signed-count-{count:x}',mutation=True,count=count,match='miss'))
    out.extend([
        dict(name='physical-entry-count-alias',mutation=True,count=256,index=1023,counter=0x123403ff,match='miss',alias='entry-count',query=0x22000),
        dict(name='physical-stack-alias',mutation=True,count=1,match='first',alias='stack'),
        dict(name='unaligned-cross-page-count',mutation=True,count=5,match='last',query=0x22fe5),
        dict(name='same-table-output-mutation',mutation=True,count=5,match='last',mapping='same-table',budget=2),
        dict(name='captured-global-divergence',mutation=True,count=5,match='last',mapping='different-root',budget=2),
        dict(name='continuation-overflow',depth=40,variant=0,budget_initial=100000),
        dict(name='normal-all-frontiers',budget_initial=1),
        dict(name='profile-all-frontiers',budget_initial=1,profile=1)])
    for fpscr in (0,0x400000,0x800000,0xc00000,0x0300009f):
        out.append(dict(name=f'fpscr-{fpscr:x}',mutation=True,count=5,match='last',budget=2,fpscr=fpscr))
    return out

def main():
    if not __debug__:raise SystemExit('Refusing optimized Python')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cost-dir',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--case-prefix',action='append',default=[]);p.add_argument('--normal-costs',action='store_true')
    p.add_argument('--negative-controls',action='store_true')
    a=p.parse_args();a.out=a.out.resolve();a.cost_dir=a.cost_dir.resolve()
    if a.out.is_relative_to(ROOT):p.error('evidence must remain outside source worktree')
    a.out.mkdir(parents=True,exist_ok=False)
    if a.negative_controls:return negative_controls(a.cost_dir,a.out)
    # Reviewed machine locations; pin object hashes and actual opcodes before
    # use. Read instrumentation never changes ARM/guest state.
    manifest={};machines={}
    for lane in ('reference','baseline','inline'):
        path=a.cost_dir/(lane+'.elf')
        if sha(path.read_bytes())!=PINNED_ELFS[lane]:raise AssertionError('machine boundary pin changed: '+lane)
        m=Machine(path,profile=a.normal_costs);m.active=False
        if (m.layout['size'],m.layout['r'],m.layout['preempt'],m.layout['f_kind'])!=(360,0,348,40):
            raise AssertionError('full-context mutation layout changed')
        m.observers={m.symbols[n]&~1:n for n in ('__wrap_xv_preempt','ct_observe_entry')}
        m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
        if lane=='reference':
            m.edge_return=0xa9b80;m.read_pcs={
                0xa9b48:('entry-map',UC_ARM_REG_R5,UC_ARM_REG_R1,4),
                0xa9b4c:('entry',UC_ARM_REG_R3,UC_ARM_REG_R1,1),
                0xa9b00:('count-map',UC_ARM_REG_R5,UC_ARM_REG_R2,4),
                0xa9b06:('count',UC_ARM_REG_R0,UC_ARM_REG_R2,1)}
            m.scan_exits={0xa90e2:'match',0xaa23a:'end'}
        else:
            delta=symbol(path,'query_fused_172c95_171f94')-0x198
            if lane=='baseline':
                m.edge_return=delta+0x416c
                offsets={0x408c:('entry-map',UC_ARM_REG_R11,UC_ARM_REG_R3,4),
                    0x4090:('entry',UC_ARM_REG_R2,UC_ARM_REG_R3,1),
                    0x40a6:('count-map',UC_ARM_REG_R11,UC_ARM_REG_R2,4),
                    0x40aa:('count',UC_ARM_REG_R3,UC_ARM_REG_R2,1)}
                exits={0x56b8:'match',0x53e2:'end'}
            else:
                m.edge_return=delta+0x3d94
                offsets={0x3372:('entry-map',UC_ARM_REG_R11,UC_ARM_REG_R4,4),
                    0x3376:('entry',UC_ARM_REG_R1,UC_ARM_REG_R4,1),
                    0x3350:('count-map',UC_ARM_REG_R11,UC_ARM_REG_R0,4),
                    0x3354:('count',UC_ARM_REG_R5,UC_ARM_REG_R1,1)}
                exits={0x337c:'match',0x3ce6:'end'}
            m.read_pcs={delta+k:v for k,v in offsets.items()};m.scan_exits={delta+k:v for k,v in exits.items()}
        # The exact callsite return instruction is recorded for independent
        # disassembly review; any changed object requires fresh annotation.
        manifest[lane]=dict(elf_sha256=sha(path.read_bytes()),edge_return=hex(m.edge_return),
            reads={hex(k):v for k,v in m.read_pcs.items()},exits={hex(k):v for k,v in m.scan_exits.items()})
        # This Unicorn build corrupts execution with even a no-op global
        # UC_HOOK_MEM_READ (retained oracle faults before any target callback).
        # Use existing code-step observations of the pinned real LDR operands.
        # This observes addresses/values without adding a native/guest load.
        m.uc.hook_add(UC_HOOK_MEM_WRITE,record_write,m);machines[lane]=m
    (a.out/'machine-boundaries.json').write_text(json.dumps(manifest,indent=2)+'\n')
    selected=([dict(name=f'normal-d{d}-v{v}',depth=d,variant=v,budget_initial=100000) for d in (1,16) for v in (0,3,4,8,12,15,1<<25)] if a.normal_costs else cases())
    rows=[]
    for case in selected:
        if a.case_prefix and not any(case['name'].startswith(x) for x in a.case_prefix):continue
        result={};expected=None
        for lane,m in machines.items():
            m.profile=a.normal_costs or case['name']=='continuation-overflow'
            m.case=case;m.call('arm_prepare',(case.get('depth',2),case.get('variant',0),case.get('budget_initial',1)))
            m.put(m.symbols['xv_object_hold_children_enabled'],case.get('profile',0))
            try:stats=m.call('arm_original' if lane=='reference' else 'arm_candidate',fpscr=case.get('fpscr',0))
            except Exception as error:
                (a.out/'failure.json').write_text(json.dumps(dict(case=case,lane=lane,error=str(error),mutations=m.mutations,
                    scan_exit=m.scan_exit,reads=m.reads,records=m.records,pc=hex(m.uc.reg_read(UC_ARM_REG_PC)),context=m.snapshot(True)),indent=2)+'\n');raise
            state=m.snapshot();observed=dict(records=m.records,reads=m.reads,scan_exit=m.scan_exit,writes=m.writes)
            if expected is None:expected=state;fp=stats['fpscr'];oracle=observed
            checks={k:state[k]==v for k,v in expected.items()}
            checks.update(fpscr=stats['fpscr']==fp,observers=m.records==oracle['records'],
                ordered_guest_writes=m.writes==oracle['writes'],scalar_reads=m.reads==oracle['reads'],scan_exit=m.scan_exit==oracle['scan_exit'])
            if case['name']=='continuation-overflow':checks['retained_generic_fallback_executed']=stats['by_function'].get('f_00087EA0',0)>0
            if case.get('mutation'):checks.update(mutation_executed=m.mutations==1,scalar_interval_executed=bool(m.reads),scan_terminated=m.scan_exit is not None)
            stats.pop('full_pc_histogram',None)
            result[lane]=dict(checks=checks,stats=stats,observers=m.records,reads=m.reads,scan_exit=m.scan_exit,
                mutation=m.mutation_summary,guest_write_count=len(m.writes))
            if not all(checks.values()):
                (a.out/'failure.json').write_text(json.dumps(dict(case=case,lane=lane,result=result),indent=2)+'\n')
                raise AssertionError((case,lane,checks))
        rows.append(dict(case=case,lanes=result));(a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('PASS',case['name'],{k:v['stats']['instructions'] for k,v in result.items()},flush=True)

if __name__=='__main__':main()
