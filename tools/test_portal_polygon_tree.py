#!/usr/bin/env python3
"""Run the typed polygon experiment inside retained whole visibility traversal.

Compare all guest RAM outside dead recursive stack frames and preserved guest
registers. Does not qualify replacing scheduler boundaries, aliasing, workers,
FPSCR/x87 scratch state, or installing this experiment on hardware.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
import test_native_visibility_portal_loop as vp
from test_arm_model_palette import RAM, CTX, SIZE
from unicorn.arm_const import UC_ARM_REG_FPSCR


def build(stage, out, negative=None):
    flags = ['-O2', '-std=gnu11', '-fno-strict-aliasing', '-ffp-contract=off',
             '-mthumb', '-mcpu=cortex-a9', '-mfpu=neon', '-ffunction-sections',
             '-fdata-sections', '-fstack-usage', '-I'+str(stage/'recomp'),
             '-I'+str(ROOT/'recomp/kernel')]
    cc = str(Path.home()/'vitasdk/bin/arm-vita-eabi-gcc')
    commands = []; objects = []
    for source in ('tools/tests/visibility_portal_arm.c', 'tools/tests/portal_polygon_arm.c',
                   'recomp/kernel/xk_portal_polygon_math.c'):
        path = ROOT/source; obj = out/(path.stem+'.o')
        cmd = [cc, *flags, '-DXP_WHOLE_VISIBILITY']
        if negative: cmd += ['-DXP_TEST_'+negative.upper().replace('-','_')]
        if path.name == 'xk_portal_polygon_math.c': cmd += ['-frounding-math']
        cmd += ['-c', str(path), '-o', str(obj)]
        subprocess.run(cmd, check=True); commands.append(cmd); objects.append(str(obj))
    retained = [stage/'build/recomp'/n for n in ('code_000.o', 'code_002.o', 'code_009.o',
        'code_016.o', 'xv_x86rt.o', 'kernel/xk_clip.o', 'kernel/xk_clip_region.o',
        'kernel/xk_clip_region_control.o', 'kernel/xk_math.o', 'kernel/xk_light_census.o')]
    for lane in ('reference','candidate'):
        cmd = [cc, *flags, *objects, *map(str,retained), '-nostdlib',
            '-Wl,-Ttext=0x10000,-e,test_boot,--unresolved-symbols=ignore-all,--wrap=xv_preempt']
        if lane == 'candidate': cmd += ['-Wl,--wrap=f_000B7F10']
        cmd += ['-lm', '-lgcc', '-o', str(out/(lane+'.elf'))]
        subprocess.run(cmd, check=True); commands.append(cmd)
    (out/'build.json').write_text(json.dumps(dict(commands=commands,
        objects={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in retained}), indent=2)+'\n')


class Machine(vp.Machine):
    def result(self, spec):
        row = self.run(spec, False)
        u = self.uc
        # Fixture entry ESP is 0x780000. Every nested original scratch frame is
        # below it; compare the return slot and every byte outside that region.
        external = bytes(u.mem_read(RAM,0x740000)) + bytes(u.mem_read(RAM+0x780000,SIZE-0x780000))
        context = bytes(u.mem_read(CTX,self.layout['size']))
        preserved = {f'r{i}':struct.unpack_from('<I',context,4*i)[0] for i in (3,4,5,6,7)}
        preserved['fsp'] = struct.unpack_from('<I',context,self.layout['fsp'])[0]
        preserved['preempt'] = struct.unpack_from('<i',context,self.layout['preempt'])[0]
        return dict(external=external, preserved=preserved, stats=row['stats'],
            context=context.hex(), fpscr=u.reg_read(UC_ARM_REG_FPSCR),
            typed=self.word(self.symbols['xp_typed_calls']),
            fallback=self.word(self.symbols['xp_fallback_calls']))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stage',type=Path,required=True); p.add_argument('--out',type=Path,required=True)
    p.add_argument('--reuse',action='store_true')
    p.add_argument('--negative-control',choices=['zero-count','shift-output'])
    a = p.parse_args(); a.out=a.out.resolve()
    if a.out.is_relative_to(ROOT): p.error('owned outputs must stay outside source')
    a.out.mkdir(parents=True,exist_ok=a.reuse)
    if not a.reuse: build(a.stage.resolve(),a.out,a.negative_control)
    specs=[dict(name='leaf',clusters=1,edges=[]),dict(name='projection',projection=True),
       dict(name='clip-empty',projection=True,portal_points=[(2,2,1),(3,2,1),(3,3,1),(2,3,1)]),
       dict(name='clip-crossing',projection=True,portal_points=[(-2,-.5,1),(2,-.5,1),(2,.5,1),(-2,.5,1)]),
       dict(name='cycle',projection=True,edges=[(0,1),(1,2),(2,0)]),
       dict(name='diamond',projection=True,edges=[(0,1),(0,2),(1,3),(2,3)]),
       dict(name='parallel-portals',projection=True,edges=[(0,1),(0,1)]),
       dict(name='noncontiguous',projection=True,noncontiguous=True),
       dict(name='yield-fallback',projection=True,budget=1,refill=1),
       dict(name='triangle',projection=True,poly_count=3),
       dict(name='chain16',projection=True,clusters=16,edges=[(i,i+1) for i in range(15)])]
    for mode in (0x400000,0x800000,0xc00000,0x0300009f):
        specs.append(dict(name=f'rounding-{mode:x}',projection=True,fpscr=mode))
    if a.negative_control: specs=[dict(name=a.negative_control,projection=True)]
    rows=[]
    for spec in specs:
        lanes={lane:Machine(a.out/(lane+'.elf')).result(spec) for lane in ('reference','candidate')}
        ref,cand=lanes.values()
        checks={k:ref[k]==cand[k] for k in ('external','preserved')}
        if not all(checks.values()):
            if ref['external']!=cand['external']:
                checks['first_difference']=next(i for i,(x,y) in enumerate(zip(ref['external'],cand['external'])) if x!=y)
        for lane,result in lanes.items():
            if not all(checks[k] for k in ('external','preserved')):
                (a.out/(spec['name']+'-'+lane+'.bin')).write_bytes(result['external'])
            result['external_sha256']=hashlib.sha256(result.pop('external')).hexdigest()
        rows.append(dict(spec=spec,checks=checks,lanes=lanes))
        (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')
        if a.negative_control:
            assert cand['typed'] > 0
            assert not checks['external'], 'negative geometry mutation was not detected'
            print('REJECTED negative control', a.negative_control, checks, flush=True)
            continue
        assert all(checks[k] for k in ('external','preserved')), (spec['name'],checks)
        if spec['name']=='yield-fallback':
            assert cand['typed']==0 and cand['fallback']>0
            assert ref['context']==cand['context'] and ref['fpscr']==cand['fpscr']
        elif spec['name']!='leaf': assert cand['typed']>0, 'typed path not exercised'
        print('PASS',spec['name'],{k:v['stats']['instructions'] for k,v in lanes.items()},
              'typed calls',cand['typed'],flush=True)


if __name__=='__main__': main()
