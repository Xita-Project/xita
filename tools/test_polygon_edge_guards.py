#!/usr/bin/env python3
"""Negative guard checks against the user's owned supported image in memory."""
import argparse
import copy
import subprocess
import sys
from pathlib import Path
from unittest.mock import patch
import gen_native_polygon_edge as gen


def run(xbe, manifest):
    image=gen.r.Image(str(xbe),str(manifest))
    refused=subprocess.run([sys.executable,'-B','-O',str(gen.ROOT/'tools/gen_native_polygon_edge.py'),
                            '--xbe',str(xbe),'--manifest',str(manifest)],capture_output=True,text=True)
    assert refused.returncode and 'generation requires assertions' in refused.stderr
    source, original=gen.generate(xbe,manifest)
    assert 'xv_math_polygon_edge' not in original
    assert 'xv_polygon_edge_begin()' in source
    changed=copy.copy(image)
    changed.data=bytes([image.data[0]^1])+image.data[1:]
    assert not gen.HaloHooks(changed).function_entry(gen.ENTRY)
    with patch.object(gen.r,'Image',return_value=changed):
        try: gen.generate(xbe,manifest)
        except AssertionError as error: assert str(error)=='unsupported image'
        else: raise AssertionError('accepted changed image')
    changed=copy.copy(image)
    def bytes_at(address,size):
        data=image.bytes_at(address,size)
        if (address,size)==(gen.ENTRY,gen.SIZE):
            data=data[:-1]+bytes([data[-1]^1])
        return data
    changed.bytes_at=bytes_at
    assert not gen.HaloHooks(changed).function_entry(gen.ENTRY)
    with patch.object(gen.r,'Image',return_value=changed):
        try: gen.generate(xbe,manifest)
        except AssertionError as error: assert str(error)=='unsupported polygon edge span'
        else: raise AssertionError('accepted changed span')
    print('PASS unsupported image/span rejected; disabled hook retains exact ordinary lift')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xbe',type=Path,required=True)
    parser.add_argument('--manifest',type=Path,required=True)
    args=parser.parse_args(); run(args.xbe,args.manifest)
