#!/usr/bin/env python3
"""Prepare the private 85020 register candidate with exact yield synchronization.

Only the pinned no-callee function is supported. Both back-edge sites have zero
relative x87 depth; no other function or runtime hook is changed.
"""
import argparse
import hashlib
from pathlib import Path
from prepare_collision_polygon_cache import REFERENCE_SHA256
REGISTERS_SHA256 = '4d5ac2e68424229e18be207682323bb4a82c3fc832980ee5cca2da958c155c5f'
SYNC = '''do { if (--c->preempt <= 0) {
        c->st[(xfsp0 + 7u) & 7u] = xr0; c->st[(xfsp0 + 6u) & 7u] = xr1; c->st[(xfsp0 + 5u) & 7u] = xr2; c->fsw = xfsw;
        xv_preempt(c);
        xfsp0 = c->fsp; xr0 = c->st[(xfsp0 + 7u) & 7u]; xr1 = c->st[(xfsp0 + 6u) & 7u]; xr2 = c->st[(xfsp0 + 5u) & 7u]; xfsw = c->fsw;
    } } while (0);'''
def prepare(reference, registers):
    for body, expected in ((reference, REFERENCE_SHA256), (registers, REGISTERS_SHA256)):
        if hashlib.sha256(body.encode()).hexdigest() != expected:
            raise ValueError('pinned polygon body changed; requalify before generating')
    if registers.count('X_PREEMPT();') != 4 or registers.count('const uint32_t xfsp0') != 1:
        raise ValueError('unexpected synchronization sites')
    return '#include "xv_x87reg.h"\n' + registers.replace('const uint32_t xfsp0', 'uint32_t xfsp0').replace('X_PREEMPT();', SYNC)
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('reference',type=Path);p.add_argument('registers',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();body=prepare(a.reference.read_text(),a.registers.read_text())
    with a.output.open('x') as f:f.write(body)
if __name__=='__main__': main()
