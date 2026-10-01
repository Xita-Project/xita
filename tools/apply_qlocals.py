#!/usr/bin/env python3
"""Apply tools/qlocals.py to every code_0*.c of a stage (in place, from the matching base-stage shard). usage: apply_q2.py <stage>"""
import collections, glob, json, os, sys
from multiprocessing import Pool
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qlocals as q
Q = os.path.dirname(os.path.abspath(__file__))
BASE = os.environ.get('QLOCALS_BASE') or sys.exit('set QLOCALS_BASE to the untransformed stage recomp/ directory')

def one(name):
    helpers = q.load_helpers(BASE)
    st = collections.Counter()
    text = open(os.path.join(BASE, name)).read()
    return name, q.transform(text, st, helpers), st

if __name__ == '__main__':
    stage = sys.argv[1]
    names = sorted(os.path.basename(f) for f in glob.glob(os.path.join(BASE, 'code_0*.c')))
    tot = collections.Counter()
    with Pool(32) as pool:
        for name, text, st in pool.imap_unordered(one, names):
            open(os.path.join(stage, 'recomp', name), 'w').write(text); tot.update(st)
    json.dump(dict(tot), open(os.path.join(stage, 'qlocals2-stats.json'), 'w'), indent=1)
    print({k: v for k, v in tot.items() if not k.startswith('barrier:')})
