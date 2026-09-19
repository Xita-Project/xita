#!/usr/bin/env python3
"""Check retirement hooks against a locally owned, audited CE executable."""
import argparse
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from recompiler.xita_recomp import Image
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925 import cluster_lifetime

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--xbe',required=True);p.add_argument('--manifest',required=True)
a=p.parse_args();image=Image(a.xbe,a.manifest);hooks=HaloHooks(image)
assert hooks.enabled, 'Expected audited CE image'
for address in cluster_lifetime.ENTRIES:
    emitted=hooks.function_entry(address)
    expected=cluster_lifetime.entry(image,address)
    assert expected and emitted[:len(expected)]==expected
    assert sum('xv_cluster_runtime_invalidate(0x' in line for line in emitted)==1
    for byte in range(16):
        class Mutated:
            def bytes_at(self, start, size):
                data=bytearray(image.bytes_at(start,size))
                if start==address:data[byte]^=1
                return bytes(data)
        assert not cluster_lifetime.entry(Mutated(),address)
assert not cluster_lifetime.entry(image,0x56670)
hooks.enabled=False
assert all(not hooks.function_entry(address) for address in cluster_lifetime.ENTRIES)
print('PASS: two early retirement hooks, 32 mutated prefixes rejected, unrelated/unsupported entries unchanged')
