#!/usr/bin/env python3
"""Run the actual world-query census adapter against guarded synthetic memory."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
    with tempfile.TemporaryDirectory(prefix='xita-query-repeat-probe-') as name:
        for sanitizer in (False,True):
            output=Path(name)/('sanitize' if sanitizer else 'normal')
            command=['cc','-std=gnu11','-O2','-fno-strict-aliasing','-Wall','-Wextra','-Werror',
                str(ROOT/'tools/tests/query_repeat_probe.c'),str(ROOT/'recomp/kernel/xk_query_repeat.c'),'-lm','-o',str(output)]
            if sanitizer:command+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
            subprocess.run(command,check=True)
            subprocess.run([str(output)],check=True)
    print('PASS: actual probe adapter: cross-page/unaligned reads, trash/overflow rejection, admission, input-only state, filter mutation, epoch/report boundaries; normal+ASan/UBSan')

if __name__=='__main__':main()
