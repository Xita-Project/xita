#!/usr/bin/env python3
"""Opt-in CE point-location hook for a hash-pinned retained shard."""
import argparse,hashlib
from pathlib import Path

def patch(text):
    start=text.index('void f_0017A8B0(xctx *restrict c)')
    end=text.index('\nvoid f_',start+5)
    body=text[start:end]
    if hashlib.sha256(body.encode()).hexdigest()!='35811f19270e8c5e14d406924f45fda37f12c21a72374ecd8b30df5b6ebce0a0':
        raise ValueError('point-location reference drift or already patched')
    wrapper='''#include "kernel/xk_point_location_hook.h"
static void point_location_guest(xctx *restrict c);
void f_0017A8B0(xctx *restrict c) { xv_point_location_hook(c,point_location_guest); }
'''
    return text[:start]+wrapper+body.replace('void f_0017A8B0(', 'static void point_location_guest(',1)+text[end:]

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('shard',type=Path);a=p.parse_args()
    result=patch(a.shard.read_text());a.shard.write_text(result)
