#!/usr/bin/env python3
"""Resolve Vita object-lock return PCs using the matching log's load anchor.

Pass the ELF belonging to the installed gameplay executable and a complete log.
Each report window remains separate; lane waits overlap and are not frame cost.
"""
from pathlib import Path
import argparse,bisect,json,re,subprocess


def resolve_windows(log, symbols):
    anchors=[s for s in symbols if s[2]=='xv_object_math_lock']
    if len(anchors)!=1:raise ValueError('ELF must contain one xv_object_math_lock anchor')
    linked=anchors[0][0]&~1
    functions=sorted(symbols);starts=[s[0]&~1 for s in functions]
    slide=None;windows=[];window=None
    for line in log.splitlines():
        match=re.search(r'code anchor ([0-9A-Fa-f]+) symbol xv_object_math_lock',line)
        if match:
            slide=(int(match[1],16)&~1)-linked
            if slide%4096:raise ValueError('Unaligned load slide; check matching ELF/log')
            window=None
        if '[object-locks] fast ' in line:
            window={'load_slide':slide,'summary':line,'sites':[]};windows.append(window)
        match=re.search(r'object-lock-site\] lane (\d+) pc ([0-9A-Fa-f]+) count (\d+) wait-us (\d+) max-us (\d+) overflow (\d+)',line)
        if not match:continue
        if slide is None:raise ValueError('Missing runtime anchor before wait sites; use a complete log')
        if window is None:raise ValueError('Missing report boundary before wait sites')
        lane,pc,count,wait,maximum,overflow=match.groups();pc=int(pc,16)
        # Thumb LR points after the call. Use its final halfword, not the next
        # instruction, which can belong to the following symbol.
        call=(pc&~1)-slide-2
        index=bisect.bisect_right(starts,call)-1
        symbol=None
        if index>=0:
            start,size,name=functions[index]
            if (start&~1)<=call<(start&~1)+size:symbol=name
        window['sites'].append(dict(lane=int(lane),runtime_return_pc=f'{pc:08X}',
            linked_call_pc=f'{call:08X}',symbol=symbol,count=int(count),
            wait_us=int(wait),max_us=int(maximum),overflow=bool(int(overflow))))
    if not any(w['sites'] for w in windows):raise ValueError('No object wait sites found')
    return [w for w in windows if w['sites']]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--elf',type=Path,required=True);p.add_argument('--log',type=Path,required=True);p.add_argument('--all',action='store_true');p.add_argument('--nm',default=str(Path.home()/'vitasdk/bin/arm-vita-eabi-nm'));a=p.parse_args()
    text=subprocess.check_output([a.nm,'-S','--defined-only',str(a.elf)],text=True)
    symbols=[(int(m[1],16),int(m[2],16),m[3]) for l in text.splitlines() if (m:=re.fullmatch(r'([0-9a-fA-F]+) ([0-9a-fA-F]+) [Tt] (.+)',l))]
    windows=resolve_windows(a.log.read_text(errors='replace'),symbols)
    print(json.dumps(dict(elf=str(a.elf),log=str(a.log),note='Use matching gameplay ELF. Per-lane waits overlap; windows are not combined.',windows=windows if a.all else windows[-1:]),indent=2))
if __name__=='__main__':main()
