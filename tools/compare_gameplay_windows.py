#!/usr/bin/env python3
"""Compare the last N ordinary 60-frame windows of two gameplay logs:
frame time/FPS, object batch wall, worker lock waits, query-unlock admissions.
Same checkpoint, ordinary play; not a paired benchmark."""
import argparse,re,statistics as st
from pathlib import Path
def rows(log,n):
    t=Path(log).read_text(errors='replace')
    fps=[float(x) for x in re.findall(r'frame time: game ([0-9.]+) ms',t)][-n:]
    batch=[int(x)/60000 for x in re.findall(r'\[object-jobs\] 60 frames .*? batch-us (\d+)',t)][-n:]
    waits=[(int(a)/60000,int(b)/60000) for a,b in re.findall(r'\[object-locks\] fast .*? wait-us (\d+)/(\d+)',t)][-n:]
    cont=[(int(a),int(b)) for a,b in re.findall(r'contended (\d+)/(\d+)',t)][-n:]
    qu=re.findall(r'\[query-unlock\] 60 frames enabled (\d+) .*?ready ([0-9/]+); unlocked ([0-9/]+) calls ([0-9/]+) us',t)[-n:]
    draw=[float(x) for x in re.findall(r'draw-hle ([0-9.]+) ms',t)][-n:]
    prep=re.findall(r'\[draw-prep\] \d+ frames \d+ draws: (setup .*?) ms/frame',t)[-n:]
    split=re.findall(r'\[job-split\] \d+ frames enabled (\d+) batches (\d+) taken front ([0-9/]+) back ([0-9/]+)',t)[-n:]
    return fps,batch,waits,cont,qu,draw,prep,split
p=argparse.ArgumentParser(description=__doc__);p.add_argument('a');p.add_argument('b');p.add_argument('-n',type=int,default=4)
a=p.parse_args()
for name,log in (('A',a.a),('B',a.b)):
    fps,batch,waits,cont,qu,draw,prep,split=rows(log,a.n)
    print(f'== {name} {log}')
    print(f'  game ms   {[round(x,1) for x in fps]}  median {st.median(fps):.1f} ms = {1000/st.median(fps):.1f} fps')
    print(f'  batch ms  {[round(x,1) for x in batch]}')
    print(f'  wait ms/lane {[ (round(x,1),round(y,1)) for x,y in waits]}  contended {cont}')
    print(f'  draw-hle  {draw}')
    for q in qu: print(f'  query-unlock enabled {q[0]} reasons {q[1]} unlocked calls {q[2]} us '+'/'.join(f'{int(x)/60000:.2f}' for x in q[3].split('/'))+' ms/frame')
    for s in split: print(f'  job-split enabled {s[0]} batches {s[1]} front {s[2]} back {s[3]}')
    for x in prep: print(f'  draw-prep {x}')
