#!/usr/bin/env python3
"""Owned-image full-state oracle and synthetic controls; no guest bytes in tests.

Run gen_native_polygon_edge.py first with the audited owned XBE/manifest.
Optional --sanitize runs the same fixtures under ASan/UBSan.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def run(directory, sanitize):
    flags=['-O2','-fno-strict-aliasing','-ffp-contract=off','-std=gnu11',
           '-I'+str(ROOT/'recomp'),'-pthread','-ffunction-sections','-fdata-sections']
    if sanitize:
        flags+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    control=ROOT/'recomp/kernel/xk_polygon_edge_control.c'
    env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0')
    commands=[]
    for name, sources, extra in [
        ('control',[ROOT/'tools/test_polygon_edge_control.c',control],[]),
        ('original',[ROOT/'tools/test_native_polygon_edge.c',
                     ROOT/'recomp/host/build/original_000B77C0.c',
                     ROOT/'recomp/kernel/xk_polygon_edge.c',control,
                     ROOT/'recomp/xv_x86rt.c'],[]),
        ('handoff',[ROOT/'tools/test_native_polygon_edge.c',
                    ROOT/'recomp/host/build/original_000B77C0.c',
                    ROOT/'recomp/kernel/xk_polygon_edge.c',control,
                    ROOT/'recomp/xv_x86rt.c'],['-DEDGE_MUTATE_HANDOFF'])]:
        target=directory/(name+('-asan' if sanitize else ''))
        command=[os.environ.get('CC','cc'),*flags,*extra,*map(str,sources),
                 '-Wl,--gc-sections','-lm','-o',str(target)]
        subprocess.run(command,check=True); commands.append(command)
        result=subprocess.run([str(target)],check=True,env=env,capture_output=True,text=True)
        print(name+': '+(result.stdout.strip() or 'PASS lifecycle, four concurrent callers, exact counters'))
        target.with_suffix('.log').write_text(result.stdout+result.stderr)
    (directory/('host-asan.json' if sanitize else 'host.json')).write_text(json.dumps(commands,indent=2)+'\n')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path)
    parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args()
    if args.output_dir:
        args.output_dir.mkdir(parents=True,exist_ok=True)
        run(args.output_dir.resolve(),args.sanitize)
    else:
        with tempfile.TemporaryDirectory(prefix='xita-edge-') as tmp:
            run(Path(tmp),args.sanitize)
