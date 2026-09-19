#!/usr/bin/env python3
"""Resolve Vita lock-return PCs against the exact build, accounting for relocation.

Requires multiple distinct instruction-boundary matches. Wait totals across
workers overlap and must not be interpreted as recoverable frame time.
"""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import subprocess


def symbolize(elf, log, prefix, windows):
    disassembly = subprocess.check_output([prefix+'objdump', '-d', str(elf)], text=True)
    returns = set()
    for line in disassembly.splitlines():
        if not re.search(r'\bblx?\b.*<(?:__)?xv_object_math_lock(?:_veneer)?>', line):
            continue
        m = re.match(r'\s*([0-9a-f]+):\s+([0-9a-f]+)(?:\s+([0-9a-f]{4}))?', line)
        if m:
            returns.add(int(m[1], 16)+(4 if m[3] else 2))
    segments = re.split(r'\[object-locks\] fast ', log)
    if len(segments)-1 < windows:
        raise ValueError('Insufficient complete lock-report windows')
    segments = segments[-windows:]
    rows = [m for segment in segments for m in re.findall(
        r'\[object-lock-site\] lane (\d+) pc ([0-9A-Fa-f]+) count (\d+) wait-us (\d+)', segment)]
    pcs = {int(row[1], 16)&~1 for row in rows}
    candidates = Counter(pc-ret for pc in pcs for ret in returns)
    best = candidates.most_common(2)
    if not best or best[0][1] < 3 or best[0][1] < len(pcs)*.8 or (len(best)>1 and best[0][1]==best[1][1]):
        raise ValueError('No uniquely supported relocation; check exact ELF and log')
    delta, matches = best[0]
    addresses = sorted(pcs)
    names = subprocess.check_output([prefix+'addr2line','-f','-e',str(elf),
        *[hex(pc-delta) for pc in addresses]], text=True).splitlines()[::2]
    names = dict(zip(addresses,names))
    totals = defaultdict(lambda: {'count':0,'wait_us':0})
    for lane,pc,count,wait in rows:
        key=(int(lane),int(pc,16)&~1)
        totals[key]['count']+=int(count);totals[key]['wait_us']+=int(wait)
    sites = [dict(lane=lane,runtime_pc=hex(pc),elf_pc=hex(pc-delta),
                  call_return_verified=pc-delta in returns,function=names[pc],**values)
             for (lane,pc),values in totals.items()]
    return dict(elf=str(elf),windows=windows,relocation=hex(delta),
        distinct_sites=len(pcs),matched_sites=matches,
        note='Overlapping lane waits, not additive frame latency; symbols require the exact build.',
        sites=sorted(sites,key=lambda x:-x['wait_us']))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('elf',type=Path);p.add_argument('log',type=Path)
    p.add_argument('--tool-prefix',default='arm-vita-eabi-')
    p.add_argument('--windows',type=int,default=6)
    a=p.parse_args()
    if a.windows<1:p.error('--windows must be positive')
    print(json.dumps(symbolize(a.elf,a.log.read_text(),a.tool_prefix,a.windows),indent=2))

if __name__=='__main__':main()
