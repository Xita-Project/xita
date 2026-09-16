#!/usr/bin/env python3
"""Compare typed private replay with the complete owned original query prefix."""
from pathlib import Path
import argparse
import json
import os
import subprocess
from test_cluster_query import ROOT, generate


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('xbe', 'manifest', 'out'):
        p.add_argument('--' + name, type=Path, required=True)
    p.add_argument('--cases', type=int, default=1024)
    p.add_argument('--tail', action='store_true', help='also execute the unchanged original allocator/list tail')
    a = p.parse_args()
    assert a.cases > 0
    a.out.mkdir(parents=True, exist_ok=True)
    generate(a.xbe, a.manifest, a.out, full=a.tail)
    binary = a.out / 'query-replay'
    command = [os.environ.get('CC', 'cc'), '-O2', '-g', '-std=gnu11',
        '-fno-strict-aliasing', '-ffp-contract=off', '-frounding-math',
        '-ffunction-sections', '-fdata-sections', '-DCASES=' + str(a.cases),
        *(['-DCLUSTER_TAIL'] if a.tail else []),
        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
        '-fno-omit-frame-pointer', '-no-pie', '-I' + str(ROOT / 'recomp'),
        '-I' + str(a.out), str(a.out / 'reference.c'),
        str(ROOT / 'tools/tests/cluster_query_replay.c'),
        str(ROOT / 'recomp/kernel/xk_cluster_query.c'),
        str(ROOT / 'recomp/kernel/xk_cluster_query_replay.c'), str(ROOT / 'recomp/xv_x86rt.c'),
        '-Wl,--gc-sections,--wrap=xv_preempt', '-lm', '-o', str(binary)]
    (a.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    subprocess.run(command, check=True)
    run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
    (a.out / 'result.log').write_text(run.stdout + run.stderr)
    print(run.stdout + run.stderr, end='')
    run.check_returncode()


if __name__ == '__main__':
    main()
