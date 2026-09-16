#!/usr/bin/env python3
"""Concurrent accounting and drained-owner control misuse; no owned inputs."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-clip-region-control-') as temp:
    binary=Path(temp)/'control'
    for sanitize in (False,True):
        command=[os.environ.get('CC','cc'),'-O2','-g','-std=gnu11','-I'+str(ROOT/'recomp'),'-pthread']
        if sanitize:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
        command +=[str(ROOT/'tools/test_clip_region_control.c'),str(ROOT/'recomp/kernel/xk_clip_region_control.c'),'-o',str(binary)]
        subprocess.run(command,check=True);subprocess.run([str(binary)],check=True)
print('PASS 200,000 concurrent completion records, exact work/max drains, disabled/-1 restoration and seven rejected owner/active misuse cases; normal + ASan/UBSan')
