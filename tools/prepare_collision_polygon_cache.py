#!/usr/bin/env python3
"""Prepare an experimental private 85020 output-page cache; never install it.

The input must be the exact retained retail function body. Generated game code
stays in the caller's private output directory. No gameplay defaults change.
"""
import argparse
import hashlib
from pathlib import Path
import re
REFERENCE_SHA256 = 'd82f84a7cac22619da0f5f30f7fe30d42bce56943467093e98ce4626305e91ba'
PREFIX = '''#ifdef XV_CHECK_GUEST_ADDRESS
#define PG_PTR(a) X_G(a)
#else
#define PG_PTR(a) ({ uint32_t pg_a = (uint32_t)(a); if (pg_tag != (pg_a >> 12)) { pg_tag = pg_a >> 12; pg_base = (uint8_t *)X_G(pg_a & ~4095u); } pg_base + (pg_a & 4095u); })
#endif
#define PG_M8(a) (*(uint8_t *)PG_PTR(a))
#define PG_M16(a) (*(xu16_u *)PG_PTR(a))
#define PG_M32(a) (*(xu32_u *)PG_PTR(a))
'''
def prepare(body):
    if hashlib.sha256(body.encode()).hexdigest() != REFERENCE_SHA256:
        raise ValueError('retained 85020 body changed; requalify before preparing a candidate')
    body = body.replace('    uint32_t fk_a',
        '    uint32_t pg_tag = UINT32_MAX; uint8_t *pg_base = NULL;\n    uint32_t fk_a', 1)
    body, count = re.subn(r'X_M(8|16|32)\(\(c->r\[1\]', r'PG_M\1((c->r[1]', body)
    if count != 22 or body.count('X_PREEMPT();') != 4:
        raise ValueError('unexpected memory/scheduling sites')
    body = body.replace('X_PREEMPT();', 'X_PREEMPT(); pg_tag = UINT32_MAX;')
    return PREFIX + body + '\n#undef PG_PTR\n#undef PG_M8\n#undef PG_M16\n#undef PG_M32\n'
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference', type=Path);p.add_argument('output', type=Path)
    a=p.parse_args();result=prepare(a.reference.read_text())
    with a.output.open('x') as f:f.write(result)
if __name__=='__main__': main()
