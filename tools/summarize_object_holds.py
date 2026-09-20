#!/usr/bin/env python3
"""Summarize [object-holds] windows: sampled outer holds, direct children of
4C980 and nested motion/query intervals. Inclusive sampled durations; rank only."""
import argparse,re,json
from collections import defaultdict
from pathlib import Path
CHILD_NOTE={0x4B9D0:'movement/collision (calls solver 49600)',0x478D0:'collection',0xBDF10:'?',0xD8B70:'?'}
MOTION={0x478D0:'collection entry',0x49600:'solver',0x172BF0:'world query',0x171F10:'collision report 171F10',
        0x170C10:'traversal 170C10',0x1721B0:'static BSP query',0x88110:'partition 88110',0x868F0:'point/matrix 868F0',0x1716F0:'1716F0'}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--windows',type=int,default=3)
    a=p.parse_args();t=a.log.read_text(errors='replace')
    segs=re.split(r'(?=\[xk\] \[object-holds\] \d+ frames lane 0 )',t)[1:]
    segs=segs[-a.windows:]
    out=dict(windows=len(segs),holds=[],children=defaultdict(lambda:[0,0,0]),motion=defaultdict(lambda:[0,0,0]),queries=defaultdict(lambda:[0,0,0]))
    for s in segs:
        for lane,en,outer,samples in re.findall(r'\[object-holds\] \d+ frames lane (\d+) enabled (\d+) outer (\d+) samples (\d+)',s):
            out['holds'].append(dict(lane=int(lane),enabled=int(en),outer=int(outer),samples=int(samples)))
        for lane,child,n,us,mx in re.findall(r'\[object-hold-child\] lane (\d+) parent 0004C980 child ([0-9A-F]+) samples (\d+) elapsed-us (\d+) max-us (\d+)',s):
            c=out['children'][child];c[0]+=int(n);c[1]+=int(us);c[2]=max(c[2],int(mx))
        for lane,fn,n,us,mx in re.findall(r'\[object-motion\] lane (\d+) function ([0-9A-F]+) samples (\d+) elapsed-us (\d+) max-us (\d+)',s):
            c=out['motion'][fn];c[0]+=int(n);c[1]+=int(us);c[2]=max(c[2],int(mx))
        for lane,route,n,us,mx in re.findall(r'\[object-query-origin\] lane (\d+) route (\S+) samples (\d+) elapsed-us (\d+) max-us (\d+)',s):
            c=out['queries'][route];c[0]+=int(n);c[1]+=int(us);c[2]=max(c[2],int(mx))
    def rows(d,names):
        return [dict(id=k,note=names.get(int(k,16),'') if names else '',samples=v[0],elapsed_us=v[1],max_us=v[2],mean_us=round(v[1]/v[0]) if v[0] else 0)
                for k,v in sorted(d.items(),key=lambda kv:-kv[1][1])]
    print(json.dumps(dict(windows=out['windows'],holds=out['holds'],children=rows(out['children'],CHILD_NOTE),
        motion=rows(out['motion'],MOTION),queries=rows(out['queries'],None),
        note='Sampled 1/64 outer holds; children/motion nested inclusive; includes parks/scheduling; do not sum.'),indent=2))
if __name__=='__main__':main()
