#!/usr/bin/env python3
"""Rank [guest-phase] scopes by self time (ms/frame) over the last N windows."""
import argparse,re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('-n',type=int,default=3);p.add_argument('--top',type=int,default=20)
a=p.parse_args();t=a.log.read_text(errors='replace')
wins=re.split(r'(?=\[xk\] \[guest-phase\] \d+ frames end-frame)',t)[1:]
wins=wins[-a.n:]
agg={}
frames=0
for w in wins:
    m=re.match(r'\[xk\] \[guest-phase\] (\d+) frames',w);frames+=int(m.group(1))
    for pc,name,calls,active,self_,parked,pself in re.findall(r'\[guest-phase\] ([0-9A-F]{8}) (\S+) calls (\d+) active-us (\d+) self-us (\d+) parked-us (\d+) parked-self-us (\d+)',w):
        d=agg.setdefault((pc,name),[0,0,0,0]);d[0]+=int(calls);d[1]+=int(active);d[2]+=int(self_);d[3]+=int(parked)
print(f'windows {len(wins)} frames {frames}')
print(f"{'scope':22s} {'calls/f':>8s} {'active ms/f':>12s} {'self ms/f':>10s} {'parked ms/f':>12s}")
for (pc,name),(c,act,se,pk) in sorted(agg.items(),key=lambda kv:-kv[1][2])[:a.top]:
    print(f'{name:22s} {c/frames:8.1f} {act/1000/frames:12.2f} {se/1000/frames:10.2f} {pk/1000/frames:12.2f}')
