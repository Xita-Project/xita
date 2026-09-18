#!/usr/bin/env python3
"""Summarize owned BSP batch sizes; these are not per-frame visibility counts."""
import argparse,hashlib,json
from pathlib import Path
from audit_visibility_dispatch import audit_bsp
from recompiler.halo_map import HaloMap
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--maps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    rows=[]
    for path in sorted(a.maps.glob('*.map')):
        game=HaloMap(str(path));bsps=[]
        for bsp in game.structure_bsps():
            audit=audit_bsp(game,bsp);counts=[r['subclusters'] for r in audit['cluster_rows']]
            bsps.append(dict(index=bsp['index'],path=audit['path'],clusters=len(counts),boxes=sum(counts),
                max_cluster_boxes=max(counts,default=0),surface_references=audit['surface_references'],
                all_clusters_budget_bound=8*sum(counts)+audit['surface_references']+len(counts),
                thresholds={str(k):dict(clusters=sum(n>=k for n in counts),boxes=sum(n for n in counts if n>=k)) for k in (8,16,24,32,64)},
                largest_clusters=sorted(audit['cluster_rows'],key=lambda x:x['subclusters'],reverse=True)[:8]))
        rows.append(dict(map=game.name,sha256=hashlib.sha256(path.read_bytes()).hexdigest(),bsps=bsps))
    if not rows:raise ValueError('no owned maps')
    result=dict(scope='Owned map structural counts, not live views or immutable-buffer proof',maps=rows)
    with a.out.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print('Audited',len(rows),'maps and',sum(len(r['bsps']) for r in rows),'BSPs')
if __name__=='__main__':main()
