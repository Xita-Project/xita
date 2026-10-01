#!/usr/bin/env python3
"""Summarize scene-qualified sampled object trees, without extrapolating FPS.

Use perf317 XV_TICK_PHASES=3 logs. Timings are rounded sampled elapsed totals;
root recursion must not be counted twice. Incomplete/open trees are rejected.
"""
import argparse
import json
import re
from pathlib import Path

DETAIL = {'90950','96430','90900','8DDF0','8BD50','8C0F0','8D760','8C570'}
ROOT_PARENTS = {'F0900002','F0900003'}

def summarize(text, limit=5):
    ready=False; current=None; rows=[]
    for line in text.splitlines(keepends=True):
        if not line.endswith('\n'):continue
        if 'frame stats:' in line:
            ready=bool(re.search(r'loaded 1 active 1.*director on 1',line))
        sample=re.search(r'\[object-sample\] (\d+) frames roots seen (\d+) selected (\d+) open (\d+);',line)
        if sample:
            if current is not None:rows.append(current)
            current=dict(zip(('frames','seen','selected','open'),map(int,sample.groups())))
            current.update(ready=ready,entries={})
        if current is not None and '[tick-phases]' in line and ' incl ' not in line:
            for parent,addr,ms,n in re.findall(r'([A-F0-9]+)>([A-F0-9]+) ([0-9.]+) \((\d+)\)',line):
                current['entries'][(parent,addr)]=(float(ms),int(n))
    # Last group needs a following sample header to prove all continuation lines
    # arrived. Conservative for both live and terminal captures.
    valid=[]; rejected=0
    for row in rows:
        if not row['ready']:continue
        e=row['entries']
        roots=[v for (p,a),v in e.items() if a=='8FB70' and p in ROOT_PARENTS]
        n=sum(v[1] for v in roots)
        if row['open'] or not n or n!=row['selected']:
            rejected+=1;continue
        total=sum(v[0] for v in roots)
        if total<=0:rejected+=1;continue
        row['root_ms_per_frame']=total;valid.append(row)
    selected=valid[-limit:]
    frames=sum(r['frames'] for r in selected)
    roots=sum(r['selected'] for r in selected)
    total=sum(r['root_ms_per_frame']*r['frames'] for r in selected)
    details={}
    for a in DETAIL:
        elapsed=sum(r['entries'].get(('8FB70',a),(0,0))[0]*r['frames'] for r in selected)
        calls=sum(r['entries'].get(('8FB70',a),(0,0))[1] for r in selected)
        details[a]={'sampled_elapsed_ms':elapsed,'calls':calls,'fraction_of_sampled_root_time':elapsed/total if total else None}
    callbacks={}
    targets={a for r in selected for (parent,a) in r['entries'] if parent=='90950'}
    type_total=details['90950']['sampled_elapsed_ms']
    for a in targets:
        elapsed=sum(r['entries'].get(('90950',a),(0,0))[0]*r['frames'] for r in selected)
        calls=sum(r['entries'].get(('90950',a),(0,0))[1] for r in selected)
        callbacks[a]={'sampled_elapsed_ms':elapsed,'calls':calls,
                      'fraction_of_sampled_type_update_time':elapsed/type_total if type_total else None}
    return {'complete_valid_windows':len(valid),'rejected_gameplay_windows':rejected,
            'selected_windows':len(selected),'frames':frames,'roots_seen':sum(r['seen'] for r in selected),
            'roots_sampled':roots,'sampled_root_elapsed_ms':total,
            'mean_elapsed_ms_per_sampled_root':total/roots if roots else None,
            'direct_components':dict(sorted(details.items(),key=lambda item:-item[1]['sampled_elapsed_ms'])),
            'type_callbacks':dict(sorted(callbacks.items(),key=lambda item:-item[1]['sampled_elapsed_ms'])),
            'note':'Rounded elapsed measurements within sampled object trees; recursive child inclusive totals excluded to prevent double counting. No full-frame extrapolation or FPS prediction. The final unclosed report group is omitted.'}

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('log',type=Path);ap.add_argument('--windows',type=int,default=5)
    args=ap.parse_args()
    if args.windows<1:ap.error('--windows must be positive')
    print(json.dumps(summarize(args.log.read_text(errors='replace'),args.windows),indent=2))
