#!/usr/bin/env python3
"""Check the diagnostic patch against a private reviewed generated shard.
No generated game source is copied into the repository.
"""
import argparse
import hashlib
import re
from pathlib import Path
from patch_object_pass_timers import transform, TAG

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('shard', type=Path)
p.add_argument('--expected-sha256', required=True)
a=p.parse_args()
source=a.shard.read_text()
m=re.search(r'^void f_000900E0\(xctx \*restrict c\)\n\{.*?^\}',source,re.M|re.S)
assert m is not None
body=m[0]
assert hashlib.sha256(body.encode()).hexdigest()==a.expected_sha256
patched=transform(source,a.expected_sha256)
assert patched.count(TAG)==8
assert transform(patched,a.expected_sha256)==patched
assert ''.join(l for l in patched.splitlines(keepends=True) if TAG not in l)==source
for old,new in (('goto L_000902A9;','goto L_0009030D;'),
                ('0009022D  mov ebp','0009022E  mov ebp')):
    bad=body.replace(old,new,1)
    assert bad!=body
    try:
        transform(source[:m.start()]+bad+source[m.end():],a.expected_sha256)
    except ValueError:
        continue
    raise AssertionError('changed object control flow accepted')
print('PASS exact recovery, both regular-pass entry paths, idempotence, layout rejection')
