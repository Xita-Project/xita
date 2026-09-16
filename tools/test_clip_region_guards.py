#!/usr/bin/env python3
"""Owned-image negative guards and full-entry versus interior admission."""
import copy,subprocess,sys
from unittest.mock import patch
import gen_native_clip_region as gen
from games.halo_ce_3925.hooks import HaloHooks
img=gen.r.Image(str(gen.ROOT/'haloce/default.xbe'),str(gen.ROOT/'local/halo_ce_3925/game_manifest.json'))
s,raw,traced=gen.generate()
for form in (raw,traced):
    h=HaloHooks(img)
    for address in (0xB7F50,0xB8000,0x117B0,0xB71C0):
        assert h.transform_body(address,'sentinel')=='sentinel'
    result=h.transform_body(0xB7F10,form[0xB7F10])
    assert result.count('xv_math_clip_region(c,')==1
    assert 'goto CLIP_REGION_INITIAL;' in result
    assert result.count('goto L_000B7F50;')==form[0xB7F10].count('goto L_000B7F50;')-1
    marked=int(form is traced)
    assert f'xv_math_clip_region(c, {marked})' in result
changed=copy.copy(img);changed.data=bytes([img.data[0]^1])+img.data[1:]
for address in (None,*gen.SPANS):
    if address is not None:
        changed=copy.copy(img)
        def bytes_at(a,n):
            data=img.bytes_at(a,n)
            return data[:-1]+bytes([data[-1]^1]) if (a,n)==(address,gen.SPANS[address][0]) else data
        changed.bytes_at=bytes_at
    assert HaloHooks(changed).transform_body(0xB7F10,raw[0xB7F10])==raw[0xB7F10]
    with patch.object(gen.r,'Image',return_value=changed):
        try:gen.generate()
        except AssertionError as error:assert str(error) in ('unsupported image','unsupported region span')
        else:raise AssertionError('accepted changed input')
refused=subprocess.run([sys.executable,'-B','-O',str(gen.ROOT/'tools/gen_native_clip_region.py')],capture_output=True,text=True)
assert refused.returncode and 'generation requires assertions' in refused.stderr
# Every hooked default phase scope remains in its original outer function.
h=HaloHooks(img);body=raw[0xB7F10].replace('{\n','{\n    XV_PHASE_SCOPE(c, 0u);\n',1)
assert h.transform_body(0xB7F10,body).count('XV_PHASE_SCOPE(c, 0u);')==1
print('PASS whole image + all four spans reject changes; interior entries unchanged; single late admission, phase cleanup retained; python -O rejected')
