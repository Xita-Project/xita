#!/usr/bin/env python3
"""Run VitaSDK-built mask bodies on Cortex-A9 instructions, not a frame-time test."""
import argparse,json,struct,subprocess
from pathlib import Path
from test_arm_cluster_runtime import RuntimeMachine,RAM,PT,SIZE
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--reference',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--thread-mapping',action='store_true');a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
cc='/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc'
flags=['-O2','-g','-fno-strict-aliasing','-mthumb','-mcpu=cortex-a9','-mfpu=neon','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'recomp')]
if a.thread_mapping:flags+=['-D__vita__','-DXV_THREAD_PAGE_TABLE=1','-DXV_RENDER_VIEW=1']
cmds=[];objects=[]
for i,src in enumerate((a.reference,ROOT/'tools/tests/surface_mask_context_arm.c',ROOT/'tools/tests/cluster_runtime_arm_imports.c')):
 obj=a.out/f'unit-{i}.o';cmd=[cc,*flags,'-c',str(src),'-o',str(obj)];subprocess.run(cmd,check=True);cmds.append(cmd);objects.append(str(obj))
names=['arm_bind','arm_prepare','arm_original','arm_candidate','arm_context_ptr','layout','mask_yields','mask_trace','g_img_base']
elf=a.out/'mask.elf';cmd=[cc,*flags,*objects,'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,'+','.join('--undefined='+n for n in names),'-lc','-lgcc','-o',str(elf)];subprocess.run(cmd,check=True);cmds.append(cmd);(a.out/'commands.json').write_text(json.dumps(cmds,indent=2))
m=RuntimeMachine(elf);m.imports={a:n for a,n in m.imports.items() if n not in ('xv_preempt','__wrap_xv_preempt')};rows=[]
for k in range(192):
 m.call('arm_bind',(PT,));m.call('arm_prepare',(k,));memory=bytes(m.uc.mem_read(RAM,SIZE));pages=bytes(m.uc.mem_read(PT,4<<20));ctx=bytes(m.uc.mem_read(m.context,m.layout['size']))
 before=m.call('arm_original');expected=bytes(m.uc.mem_read(RAM,SIZE));ep=bytes(m.uc.mem_read(PT,4<<20));ec=bytes(m.uc.mem_read(m.context,m.layout['size']));ny=struct.unpack('<I',m.uc.mem_read(m.symbols['mask_yields'],4))[0];trace=bytes(m.uc.mem_read(m.symbols['mask_trace'],ny*m.layout['size']))
 m.uc.mem_write(RAM,memory);m.uc.mem_write(PT,pages);m.uc.mem_write(m.context,ctx);m.uc.mem_write(m.symbols['mask_yields'],bytes(4))
 after=m.call('arm_candidate');checks=[bytes(m.uc.mem_read(RAM,SIZE))==expected,bytes(m.uc.mem_read(PT,4<<20))==ep,bytes(m.uc.mem_read(m.context,m.layout['size']))==ec,struct.unpack('<I',m.uc.mem_read(m.symbols['mask_yields'],4))[0]==ny,bytes(m.uc.mem_read(m.symbols['mask_trace'],ny*m.layout['size']))==trace,before['fpscr']==after['fpscr']];assert all(checks),(k,checks)
 rows.append(dict(case=k,yields=ny,original=before,candidate=after))
 if k%32==31:print('passed',k+1,'Vita-linked cases',flush=True)
(a.out/'result.json').write_text(json.dumps(rows,indent=2));print('PASS',len(rows),'context/arena/mapping/handoff/FPSCR comparisons')
