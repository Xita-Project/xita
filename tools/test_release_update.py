#!/usr/bin/env python3
"""Production downloader state machine with deterministic transport; no device/network writes."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
    exe=Path(d)/'release-test'
    subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
        '-DXV_RELEASE_HOST_TEST=1','-I'+str(ROOT/'runtime'),str(ROOT/'runtime/xv_release.c'),
        str(ROOT/'runtime/xv_sha256.c'),str(ROOT/'tools/tests/release_test.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],cwd=d,check=True)
    obj=Path(d)/'remote.o'
    subprocess.run(['cc','-O2','-c',str(ROOT/'runtime/xv_remote.c'),'-o',str(obj)],check=True)
    symbols=subprocess.check_output(['nm','-u',str(obj)],text=True)
    assert not symbols.strip(),symbols
    assert b'/update/' not in obj.read_bytes() and b'remote.key' not in obj.read_bytes()
    print('PASS: default tester remote object has no external calls or server endpoint data')
