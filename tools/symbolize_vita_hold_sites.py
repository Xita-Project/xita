#!/usr/bin/env python3
"""Resolve sampled lock-holder PCs ([object-hold-site]) like the lock-site tool.

Hold PCs are the same xv_object_math_lock return addresses, so the relocation is
recovered identically. Sampled elapsed is 1/64 of outer holds and includes
scheduling/parks; it ranks holders, it is not total hold time.
"""
import argparse,json,re,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import symbolize_vita_lock_sites as base

def rewrite(log,windows):
    segments=re.split(r'\[object-holds\] \d+ frames lane 0 ',log)
    if len(segments)-1<windows:raise ValueError('Insufficient hold-report windows')
    out=[]
    for seg in segments[-windows:]:
        out.append('[object-locks] fast ')
        for lane,pc,samples,us in re.findall(r'\[object-hold-site\] lane (\d+) pc ([0-9A-Fa-f]+) samples (\d+) elapsed-us (\d+)',seg):
            out.append(f'[object-lock-site] lane {lane} pc {pc} count {samples} wait-us {us}')
    return '\n'.join(out)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf',type=Path);p.add_argument('log',type=Path)
    p.add_argument('--tool-prefix',default='arm-vita-eabi-')
    p.add_argument('--windows',type=int,default=3)
    a=p.parse_args()
    r=base.symbolize(a.elf,rewrite(a.log.read_text(),a.windows),a.tool_prefix,a.windows)
    r['note']='Sampled (1/64) outer hold elapsed by lock-site pc; includes parks/scheduling; ranks holders only.'
    for s in r['sites']:s['samples']=s.pop('count');s['sampled_elapsed_us']=s.pop('wait_us')
    print(json.dumps(r,indent=2))
if __name__=='__main__':main()
