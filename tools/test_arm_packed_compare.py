#!/usr/bin/env python3
"""Check actual ARM packed comparisons, read bounds and instruction counts (not cycles)."""
import argparse, json, subprocess
from pathlib import Path
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *
ROOT = Path(__file__).resolve().parents[1]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--sdk', type=Path, default=Path.home()/'vitasdk')
    a = p.parse_args(); a.out.mkdir(parents=True, exist_ok=True)
    fixture = a.out/'compare.c'
    fixture.write_text('#include "xv_packed_vertex.h"\nint test_compare(const void *a,const void *b,unsigned n){return xv_packed_equal(a,b,n);}\n')
    rows = []
    for wide in (0, 1):
        binary = a.out/f'compare-{wide}.elf'
        subprocess.run([str(a.sdk/'bin/arm-vita-eabi-gcc'), '-O2', '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon', '-nostdlib', '-Wall', '-Wextra', '-Werror', '-DXV_PACKED_VERTEX_LAYOUT=1', f'-DXV_VERTEX_WIDE_COMPARE={wide}', '-I'+str(ROOT/'runtime'), str(fixture), '-Wl,-Ttext=0x10000,-e,test_compare', '-o', str(binary)], check=True)
        u = Uc(UC_ARCH_ARM, UC_MODE_ARM); u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
        u.reg_write(UC_ARM_REG_C1_C0_2,15<<20);u.reg_write(UC_ARM_REG_FPEXC,1<<30)
        with binary.open('rb') as f:
            elf = ELFFile(f); symbols = {s.name:s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
            pages = set()
            for seg in elf.iter_segments():
                if seg['p_type'] != 'PT_LOAD': continue
                lo,n = seg['p_vaddr'],seg['p_memsz']
                for page in range(lo&~4095,(lo+n+4095)&~4095,4096):
                    if page not in pages: u.mem_map(page,4096);pages.add(page)
                u.mem_write(lo,seg.data())
        A,B,S,END = 0x200000,0x300000,0x400000,0x500000
        for addr in (A,B,S,END):u.mem_map(addr,65536)
        state = {'instructions':0,'a':0,'b':0,'vertices':0}
        def code(uc,addr,size,data):state['instructions'] += 1
        def read(uc,kind,addr,size,value,data):
            if S<=addr and addr+size<=S+65536:return
            offset=addr-state['a']
            source = offset>=0 and offset+size<=state['vertices']*32 and offset%32+size<=16
            packed = state['b']<=addr and addr+size<=state['b']+state['vertices']*16
            assert source or packed, ('out-of-prefix read',hex(addr),size,state)
        def write(uc,kind,addr,size,value,data):
            assert S<=addr and addr+size<=S+65536, ('input modified',hex(addr),size)
        u.hook_add(UC_HOOK_CODE,code);u.hook_add(UC_HOOK_MEM_READ,read);u.hook_add(UC_HOOK_MEM_WRITE,write)
        for n in list(range(21))+[31,32,63,64,127,128,257,1024]:
            for offset in (0,1,15):
                src=bytes((i*13+i//32*17)&255 for i in range(n*32))
                base=b''.join(src[i:i+16] for i in range(0,len(src),32))
                cases=[('equal',None),('unused-tail',None)]
                if n:cases += [('first',0),('middle',(n//2)*16+7),('last',n*16-1)]
                for name,changed in cases:
                    source=bytearray(src);packed=bytearray(base)
                    if name=='unused-tail' and n:source[-1]^=1
                    if changed is not None:packed[changed]^=1
                    if source:u.mem_write(A+offset,bytes(source))
                    if packed:u.mem_write(B+offset,bytes(packed))
                    state.update(instructions=0,a=A+offset,b=B+offset,vertices=n)
                    for reg,val in [(UC_ARM_REG_R0,A+offset),(UC_ARM_REG_R1,B+offset),(UC_ARM_REG_R2,n),(UC_ARM_REG_SP,S+65024),(UC_ARM_REG_LR,END|1)]:u.reg_write(reg,val)
                    u.emu_start(symbols['test_compare'],END,count=1000000)
                    assert u.reg_read(UC_ARM_REG_PC)==END
                    assert u.reg_read(UC_ARM_REG_R0)==int(changed is None),(wide,n,offset,name)
                    assert bytes(u.mem_read(A+offset,len(source)))==source
                    assert bytes(u.mem_read(B+offset,len(packed)))==packed
                    rows.append(dict(wide=wide,vertices=n,offset=offset,case=name,instructions=state['instructions']))
    result={'result':'PASS','cases':len(rows),'rows':rows,'scope':'Actual ARM output equality and prefix-only reads; instruction counts are not timing or FPS.'}
    (a.out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS',len(rows),'ARM packed comparisons; exact results, input immutability, bounded prefix-only reads')
    for row in rows:
        if row['offset']==0 and row['vertices']==1024 and row['case']=='equal':print(row)

if __name__=='__main__':
    if not __debug__:raise SystemExit('Run without Python -O')
    main()
