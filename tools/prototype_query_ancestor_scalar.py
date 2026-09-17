#!/usr/bin/env python3
"""Private first cost gate for ordered ancestor membership state reconstruction.

This is NOT the rejected block/lookahead proposal. Each entry and following
count are read at their original scalar iteration using the captured roots.
No shared header, generic body, vertex scan, solver or guest store is changed.
Owned source/object/ELF outputs must remain outside this worktree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT)]
from test_arm_cluster_runtime import RuntimeMachine, RAM, SIZE, STACK
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_FPSCR

START = 'L_00087F7C:\n'
END = 'L_00087F8F:\n'
FLAG = 'XV_QUERY_ANCESTOR_SCALAR'
RETAINED_TEXT_SHA256 = '06b9b249c7a014175c27d624ec682c014d9f17cd2fc609f5efac05f52336d0fa'
REPLACEMENT = '''L_00087F7C:
    q1=X_M32(q3);q0=0;
    {
        int32_t nq_ancestor_budget=c->preempt;
        uint32_t nq_ancestor_carry=c->f_cf;
        for(;;) {
            /* Retain scalar captured-root reads in original order. */
            __asm__ volatile("" ::: "memory");
            uint32_t nq_ancestor_value=X_M32(q6+q0*4u+0x1Cu);
            if(nq_ancestor_value==q1) {
                c->preempt=nq_ancestor_budget;c->f_cf=nq_ancestor_carry;
                X_FLAGS(XK_SUB,nq_ancestor_value,q1,nq_ancestor_value-q1,32);
                goto L_00087FE7;
            }
            nq_ancestor_carry=nq_ancestor_value<q1;
            q2++;q0=(uint32_t)(int32_t)(int16_t)q2;
            __asm__ volatile("" ::: "memory");
            uint32_t nq_ancestor_count=X_M32(q6+0x18u);
            if((int32_t)q0>=(int32_t)nq_ancestor_count) {
                c->preempt=nq_ancestor_budget;c->f_cf=nq_ancestor_carry;
                X_FLAGS(XK_SUB,q0,nq_ancestor_count,q0-nq_ancestor_count,32);
                break;
            }
            if(--nq_ancestor_budget<=0) {
                c->preempt=nq_ancestor_budget;c->f_cf=nq_ancestor_carry;
                X_FLAGS(XK_SUB,q0,nq_ancestor_count,q0-nq_ancestor_count,32);
                NQ_SAVE(); memcpy(guest,c,sizeof *c);
                xv_preempt(guest);
                memcpy(c,guest,sizeof *c); NQ_LOAD();
                nq_ancestor_budget=c->preempt;nq_ancestor_carry=c->f_cf;
            }
        }
    }
'''

def sha(data): return hashlib.sha256(data).hexdigest()

def transform(source):
    if source.count(START)!=1 or source.count(END)!=1:
        raise ValueError('ancestor scan labels changed')
    first=source.index(START);last=source.index(END,first)
    original=source[first:last]
    if sha(original.encode())!='8b7f876a00567e70d06aa99f883785037cf0320fa8214c7b3219d400b75c50c7':
        raise ValueError('ancestor source interval drift')
    for label in ('L_00087F80','L_00087F86'):
        if label in source[:first]+source[last:]:raise ValueError('external interior entry')
    guarded='#if '+FLAG+'\n'+REPLACEMENT+'#else\n'+original+'#endif\n'
    prefix='#ifndef '+FLAG+'\n#define '+FLAG+' 0\n#endif\n#if '+FLAG+' != 0 && '+FLAG+' != 1\n#error "'+FLAG+' must be 0 or 1"\n#endif\n'
    changed=prefix+source[:first]+guarded+source[last:]
    assert changed.removeprefix(prefix).replace(guarded,original,1)==source
    return changed,original

def allocated(path):
    with path.open('rb') as stream:
        elf=ELFFile(stream)
        sections={s.name:s.data().hex() for s in elf.iter_sections() if s['sh_flags']&2 and s['sh_type']!='SHT_NOBITS'}
        relocs={}
        for section in elf.iter_sections():
            if section['sh_type'] not in ('SHT_REL','SHT_RELA'):continue
            target=elf.get_section(section['sh_info'])
            if not(target['sh_flags']&2):continue
            symbols=elf.get_section(section['sh_link'])
            relocs[section.name]=[(r['r_offset'],r['r_info_type'],symbols.get_symbol(r['r_info_sym']).name) for r in section.iter_relocations()]
        return sections,relocs

class Machine(RuntimeMachine):
    def word(self,a):return struct.unpack('<I',self.uc.mem_read(a,4))[0]
    def snapshot(self,hashed=False):
        arena=bytes(self.uc.mem_read(RAM,SIZE))
        out=dict(context=bytes(self.uc.mem_read(self.context,self.layout['size'])).hex(),
                 arena=sha(arena) if hashed else arena,
                 original_pages=bytes(self.uc.mem_read(self.symbols['ct_original_pages'],4096)).hex(),
                 alternate_pages=bytes(self.uc.mem_read(self.symbols['ct_alternate_pages'],4096)).hex())
        for n in ('ct_yields','ct_events','ct_seen','ct_boundary','ct_collect_calls','g_xram','g_img_base'):
            out[n]=self.word(self.symbols[n])
        p=self.word(self.symbols['g_xpt'])
        out['g_xpt']='original' if p==self.symbols['ct_original_pages'] else 'alternate' if p==self.symbols['ct_alternate_pages'] else p
        return out
    def step(self,uc,address,size,user):
        self.min_sp=min(self.min_sp,uc.reg_read(UC_ARM_REG_SP))
        if self.active and address in self.observers:
            if uc.reg_read(UC_ARM_REG_R0)!=self.context:raise AssertionError('wrong observer identity')
            item=self.snapshot(True);item.update(name=self.observers[address],fpscr=uc.reg_read(UC_ARM_REG_FPSCR),
                tag=uc.reg_read(UC_ARM_REG_R1) if self.observers[address]=='ct_observe_entry' else 0)
            self.records.append(item)
        super().step(uc,address,size,user)
    def call(self,name,*args,**kwargs):
        self.active=name in ('arm_original','arm_candidate');self.writes=[];self.records=[];self.min_sp=0xffffffff
        stats=super().call(name,*args,**kwargs);self.active=False
        stats['peak_stack_bytes']=STACK+65024-self.min_sp
        stats['full_pc_histogram']={hex(a):n for a,n in sorted(self.by_address.items())}
        return stats

def record_write(uc,access,address,size,value,m):
    if m.active and RAM<=address and address+size<=RAM+SIZE:
        m.writes.append((address-RAM,size,value&((1<<(8*size))-1)))

def main():
    if not __debug__:raise SystemExit('Refusing optimized Python')
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cost-dir',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();a.out=a.out.resolve();a.cost_dir=a.cost_dir.resolve()
    if a.out.is_relative_to(ROOT):p.error('owned output must remain outside source worktree')
    a.out.mkdir(parents=True,exist_ok=False)
    source=(a.cost_dir/'inline.c').read_text();changed,original=transform(source)
    pin=allocated(a.cost_dir/'inline.o')
    if sha(bytes.fromhex(pin[0]['.text']))!=RETAINED_TEXT_SHA256:raise AssertionError('unreviewed current semantic query')
    recorded=json.loads((a.cost_dir/'build.json').read_text())['commands'][1]
    base_command=recorded['command'];compile_cwd=recorded['cwd']
    commands=[];reports={}
    cc=base_command[0];objdump=cc.removesuffix('gcc')+'objdump';nm=cc.removesuffix('gcc')+'nm'
    for lane in ('baseline','inline'):
        file=a.out/(lane+'.c');file.write_text(changed)
        command=list(base_command);command[command.index('-c')+1]=str(file);command[command.index('-o')+1]=str(a.out/(lane+'.o'))
        command.insert(1,'-D'+FLAG+'='+str(int(lane=='inline')))
        subprocess.run(command,cwd=compile_cwd,check=True);commands.append(dict(command=command,cwd=compile_cwd))
        (a.out/(lane+'-make.log')).write_text(shlex.join(command)+'\n')
        sec,_=allocated(a.out/(lane+'.o'))
        with (a.out/(lane+'.asm')).open('w') as stream:subprocess.run([objdump,'-dr',str(a.out/(lane+'.o'))],stdout=stream,check=True)
        reports[lane]=dict(text_bytes=len(bytes.fromhex(sec['.text'])),text_sha256=sha(bytes.fromhex(sec['.text'])),
            stack=(a.out/(lane+'.su')).read_text(),imports=subprocess.check_output([nm,'-u',str(a.out/(lane+'.o'))],text=True))
    if allocated(a.out/'baseline.o')!=pin:raise AssertionError('OFF allocated object/relocations changed')
    links=json.loads((a.cost_dir/'execution-commands.json').read_text());saved=[links[0],links[1]]
    shutil.copy2(a.cost_dir/'reference.elf',a.out/'reference.elf')
    for lane in ('baseline','inline'):
        command=list(links[3]);old=str(a.cost_dir/'inline.o')
        assert command.count(old)==1;command[command.index(old)]=str(a.out/(lane+'.o'));command[-1]=str(a.out/(lane+'.elf'))
        subprocess.run(command,check=True);saved.append(command)
    (a.out/'execution-commands.json').write_text(json.dumps(saved,indent=2)+'\n')
    (a.out/'build.json').write_text(json.dumps(dict(commands=commands,results=reports,
        off_allocated_sections_and_relocations_identical=True,original_interval_sha256=sha(original.encode()),
        default=0,lookahead=False,hoisted_guest_read=False,guest_stores_changed=False),indent=2)+'\n')
    machines={}
    for lane in ('reference','baseline','inline'):
        m=Machine(a.out/(lane+'.elf'),profile=True);m.active=False
        m.observers={m.symbols[n]&~1:n for n in ('__wrap_xv_preempt','ct_observe_entry')}
        m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
        m.uc.hook_add(UC_HOOK_MEM_WRITE,record_write,m);machines[lane]=m
    rows=[]
    for name,depth,variant in [('ordinary',16,0),('four-surface',16,3),('short',1,0),('empty',1,8),('combined',16,0x2000000),('outside4',16,15)]:
        expected=None;result={}
        for lane,m in machines.items():
            m.call('arm_prepare',(depth,variant,100000))
            stats=m.call('arm_original' if lane=='reference' else 'arm_candidate')
            actual=m.snapshot()
            if expected is None:expected=actual;fp=stats['fpscr'];writes=m.writes;observers=m.records
            checks={k:actual[k]==v for k,v in expected.items()}
            checks.update(fpscr=stats['fpscr']==fp,ordered_guest_writes=m.writes==writes,observers=m.records==observers)
            result[lane]=dict(stats=stats,checks=checks,observers=m.records,guest_write_events=len(m.writes))
            if not all(checks.values()):
                (a.out/'failure.json').write_text(json.dumps(dict(case=name,lane=lane,result=result),indent=2)+'\n')
                raise AssertionError((name,lane,checks))
        rows.append(dict(name=name,depth=depth,variant=variant,lanes=result))
        (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('PASS whole query',name,{k:v['stats']['instructions'] for k,v in result.items()},flush=True)

if __name__=='__main__':main()
