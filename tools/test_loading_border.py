#!/usr/bin/env python3
"""Check loading specialization and border weights against discrete tap sampling."""
import math
import random
from pathlib import Path
from specialize_loading_border import specialize


def reference(tex, u, v, border, axes, linear):
    h, w = len(tex), len(tex[0])
    def tap(x, y):
        if (axes & 1 and not 0 <= x < w) or (axes & 2 and not 0 <= y < h):
            return border
        return tex[min(h-1, max(0, y))][min(w-1, max(0, x))]
    if not linear:
        return tap(math.floor(u*w), math.floor(v*h))
    x, y = u*w-.5, v*h-.5
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x-ix, y-iy
    return sum(tap(ix+dx, iy+dy)*(fx if dx else 1-fx)*(fy if dy else 1-fy)
               for dx in (0,1) for dy in (0,1))


def corrected(tex, u, v, border, axes, linear):
    def coverage(q, n):
        clamp=lambda x:max(0.,min(1.,x))
        return clamp(q*n+.5)*clamp((1-q)*n+.5) if linear else float(0 <= q < 1)
    weight=(coverage(u,len(tex[0])) if axes&1 else 1)*(coverage(v,len(tex)) if axes&2 else 1)
    return border*(1-weight)+reference(tex,u,v,border,0,linear)*weight

rng=random.Random(3925)
count=0
for w,h in [(1,1),(2,3),(128,16)]:
    tex=[[rng.random() for x in range(w)] for y in range(h)]
    for axes in range(4):
        for linear in (False,True):
            coords=[(-1,-1),(0,0),(1,1),(-.5/w,.5/h),(.5/w,.5/h),(1-.5/w,1-.5/h),(1+.5/w,1+.5/h)]
            coords += [(rng.uniform(-.1,1.1),rng.uniform(-.1,1.1)) for _ in range(100)]
            for u,v in coords:
                b=rng.random()
                assert abs(corrected(tex,u,v,b,axes,linear)-reference(tex,u,v,b,axes,linear))<1e-12
                count+=1
source='float4 main(VertOut IN, uniform float4 xv_atest) : COLOR { float4 t1 = tex2D(tex1, IN.texcoord1.xy); return t1; }'
out=specialize(source)
assert 'uniform float4 xv_border1[2]' in out and out.count('tex2D(')==1
assert 'inside1.x * inside1.y' in out
try:specialize('unexpected shader')
except ValueError:pass
else:raise AssertionError('must reject unexpected sample layout')
print(f'PASS: {count} point/bilinear border cases, one sample retained, unexpected layout rejected')
