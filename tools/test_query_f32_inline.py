#!/usr/bin/env python3
"""Targeted observations of the exact production query-inline prototype.

Uses the unmodified retained generic objects and the actual production-built
query units from prototype_query_f32_inline.py. Observer records compare full
contexts, full-arena hashes, both complete page tables, roots and FPSCR. Final
arena/context comparisons are byte-for-byte. Callback mutations are authored
fixture behavior, applied identically after the real fixture callback returns.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import shlex
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
from test_arm_cluster_runtime import RuntimeMachine, RAM, SIZE, STACK
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_FPSCR


def sha(data): return hashlib.sha256(data).hexdigest()


def main():
    if not __debug__: raise SystemExit('Refusing optimized Python')
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cost-dir', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args(); a.cost_dir = a.cost_dir.resolve(); a.out = a.out.resolve()
    if a.out.is_relative_to(ROOT): p.error('private output must be outside source worktree')
    a.out.mkdir(parents=True, exist_ok=False)
    links = json.loads((a.cost_dir / 'execution-commands.json').read_text())
    stage = a.cost_dir / 'build'
    commands = []
    # Bounded overflow is a separate fixture variant, never the proposed
    # production code or a performance result. Generic fallback stays original.
    def overflow(lane):
        source = (a.cost_dir / (lane + '.c')).read_text()
        assert source.count('struct frame frames[32];') == 1
        assert source.count('if(depth==32)') == 8
        source = source.replace('struct frame frames[32];', 'struct frame frames[1];').replace('if(depth==32)', 'if(depth==1)')
        path = a.out / (lane + '-overflow.c'); path.write_text(source)
        line = next(s for s in (a.cost_dir / (lane + '-make.log')).read_text().splitlines() if ' -c recomp/query_fusion.c ' in s)
        command = shlex.split(line)
        command[command.index('-c') + 1] = str(path)
        command[command.index('-o') + 1] = str(a.out / (lane + '-overflow.o'))
        subprocess.run(command, cwd=stage, check=True); commands.append(command)
        link = list(links[2 if lane == 'baseline' else 3])
        old = str(a.cost_dir / (lane + '.o'))
        assert link.count(old) == 1
        link[link.index(old)] = str(a.out / (lane + '-overflow.o'))
        link[-1] = str(a.out / (lane + '-overflow.elf'))
        subprocess.run(link, check=True); commands.append(link)
        return Path(link[-1])
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = {lane: pool.submit(overflow, lane) for lane in ('baseline','inline')}
        overflow_paths = {lane: future.result() for lane,future in futures.items()}
    (a.out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')

    class Machine(RuntimeMachine):
        def word(self, address): return struct.unpack('<I', self.uc.mem_read(address,4))[0]
        def symbol_word(self, name): return self.word(self.symbols[name])
        def put_word(self, address, value): self.uc.mem_write(address, struct.pack('<I',value))
        def guest_address(self, address):
            return self.symbol_word('g_xram') + self.word(self.symbol_word('g_xpt') + 4*(address>>12)) + (address&4095)
        def guest_read(self, address, length): return bytes(self.uc.mem_read(self.guest_address(address+i),1)[0] for i in range(length))
        def guest_write(self, address, data):
            for i,value in enumerate(data): self.uc.mem_write(self.guest_address(address+i),bytes([value]))
        def snapshot(self, hashed=False):
            memory = bytes(self.uc.mem_read(RAM,SIZE))
            result = dict(context=bytes(self.uc.mem_read(self.context,self.layout['size'])).hex(),
                          memory=sha(memory) if hashed else memory,
                          original_pages=bytes(self.uc.mem_read(self.symbols['ct_original_pages'],4096)).hex(),
                          alternate_pages=bytes(self.uc.mem_read(self.symbols['ct_alternate_pages'],4096)).hex(),
                          arena=self.symbol_word('g_xram'),image=self.symbol_word('g_img_base'))
            root = self.symbol_word('g_xpt')
            result['root'] = 'original' if root==self.symbols['ct_original_pages'] else 'alternate' if root==self.symbols['ct_alternate_pages'] else root
            for name in ('ct_yields','ct_events','ct_seen','ct_boundary','ct_collect_calls'):
                result[name] = self.symbol_word(name)
            return result
        def callback_mutation(self):
            root = self.symbol_word('g_xpt')
            if self.change == 'root-plane':
                replacement = self.symbols['ct_alternate_pages']
                self.uc.mem_write(replacement, bytes(self.uc.mem_read(root,4096)))
                root = replacement; self.put_word(self.symbols['g_xpt'],root)
            # A different physical page AND different float value make stale
            # translation/value reuse observable; no guest operation is replayed.
            old = self.word(root + 4*0x13)
            self.uc.mem_write(RAM+0x303000, bytes(self.uc.mem_read(RAM+old,4096)))
            self.put_word(root + 4*0x13,0x303000)
            self.guest_write(0x1300c,struct.pack('<f',.125))
            self.mutations += 1
        def step(self, uc, address, size, user):
            self.min_sp = min(self.min_sp,uc.reg_read(UC_ARM_REG_SP))
            if self.active:
                if address == self.pending:
                    self.pending = None; self.callback_mutation()
                name = self.observers.get(address)
                if name:
                    if uc.reg_read(UC_ARM_REG_R0) != self.context or self.symbol_word('ct_current_context') != self.context:
                        raise AssertionError('observer received a private or stale context pointer')
                    event = self.snapshot(True)
                    event.update(observer=name, fpscr=uc.reg_read(UC_ARM_REG_FPSCR),
                                 tag=uc.reg_read(UC_ARM_REG_R1) if name=='ct_observe_entry' else 0)
                    self.records.append(event)
                    if name=='__wrap_xv_preempt' and self.change and not self.mutations and self.pending is None:
                        self.pending = uc.reg_read(UC_ARM_REG_LR)&~1
                if address in self.generic_entries: self.generic_calls += 1
            super().step(uc,address,size,user)
        def call(self, name, *args, **kw):
            self.active = name in ('arm_original','arm_candidate')
            self.records=[];self.pending=None;self.mutations=0;self.generic_calls=0;self.min_sp=0xffffffff
            result = super().call(name,*args,**kw)
            self.active=False
            result.update(peak_stack_bytes=STACK+65024-self.min_sp, callback_mutations=self.mutations,
                          generic_child_entries=self.generic_calls)
            return result

    paths = {lane: a.cost_dir / (lane+'.elf') for lane in ('reference','baseline','inline')}
    paths.update({lane+'-overflow':path for lane,path in overflow_paths.items()})
    machines = {}
    for lane,path in paths.items():
        m=Machine(path,profile=True);m.active=False;m.change=None
        m.observers={m.symbols[n]&~1:n for n in ('__wrap_xv_preempt','ct_observe_entry')}
        m.generic_entries={m.symbols[n]&~1 for n in ('f_00087EA0','f_00087E10','f_00086F50','f_000B0CB0')}
        m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
        machines[lane]=m
    cases=[dict(name='unaligned-stack',variant=3),dict(name='cross-page-center',cross_page=True),
           dict(name='physical-center-output-alias',alias=True),
           dict(name='profile-active',profile=1),dict(name='profile-yields',profile=1,budget=1),
           dict(name='root-plane-change',budget=1,change='root-plane'),
           dict(name='same-table-plane-change',budget=1,change='same-table-plane'),
           dict(name='original-root-replacement',variant=5<<14,budget=1)]
    for fp in (0,0x400000,0x800000,0xc00000,0x0300009f):
        cases.append(dict(name='rounded-fpscr-'+hex(fp),variant=(14<<28)|(1<<24)|(7<<8),fpscr=fp))
    for profile in (9,12,13):
        cases.append(dict(name='exceptional-'+str(profile),variant=(profile<<28)|(1<<24),fpscr=0x0300009f))
    for change in (None,'root-plane','same-table-plane'):
        cases.append(dict(name='overflow-'+str(change),budget=1,change=change,overflow=True))
    rows=[]
    for case in cases:
        expected=None;expected_observers=None;result={}
        lanes=('reference','baseline-overflow','inline-overflow') if case.get('overflow') else ('reference','baseline','inline')
        for lane in lanes:
            m=machines[lane];m.change=case.get('change')
            m.call('arm_prepare',(3,case.get('variant',0),case.get('budget',100000)))
            m.put_word(m.symbols['xv_object_hold_children_enabled'],case.get('profile',0))
            sp=m.word(m.context+m.layout['r']+16)
            if case.get('cross_page'):
                center=m.guest_read(0x1d000,12);m.guest_write(0x1dffe,center)
                m.guest_write(sp+4,struct.pack('<I',0x1dffe))
            if case.get('alias'):
                center=m.guest_read(0x1d000,12);root=m.symbol_word('g_xpt')
                m.put_word(root+4*0x1d,m.word(root+4*0x20));m.guest_write(0x1d000,center)
            stats=m.call('arm_original' if lane=='reference' else 'arm_candidate',fpscr=case.get('fpscr',0))
            state=m.snapshot()
            if expected is None: expected=state;expected_observers=m.records;expected_fp=stats['fpscr']
            checks={key:state[key]==value for key,value in expected.items()}
            checks.update(fpscr=stats['fpscr']==expected_fp,observers=m.records==expected_observers)
            if case.get('change'): checks['mutation_executed']=stats['callback_mutations']==1
            if case.get('overflow') and lane!='reference':checks['generic_fallback_executed']=stats['generic_child_entries']>0
            result[lane]=dict(checks=checks,stats=stats,observer_count=len(m.records),observers=m.records)
            if not all(checks.values()):
                (a.out/'failure.json').write_text(json.dumps(dict(case=case,lane=lane,result=result),indent=2)+'\n')
                raise AssertionError((case,lane,checks))
        rows.append(dict(case=case,lanes=result))
        (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('PASS observer query',case['name'],{k:v['observer_count'] for k,v in result.items()},flush=True)
    (a.out/'receipt.json').write_text(json.dumps(dict(result='PASS',cases=len(rows),
        strict_context_arena_pages_roots_fpscr=True,original_observer_identity=True,
        inputs={str(path):sha(path.read_bytes()) for path in paths.values()},
        limitations=['Synthetic callback mutations; firmware copies modeled.','No hardware/FPS result.']),indent=2)+'\n')


if __name__=='__main__':main()
