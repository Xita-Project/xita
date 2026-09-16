#!/usr/bin/env python3
"""Test owned cluster snapshots, failure cleanup and reader retirement."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--sanitize', choices=('address', 'thread', 'none'), default='address')
    a = parser.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    binary = a.out / 'snapshot-test'
    command = [os.environ.get('CC', 'cc'), '-O2', '-g', '-std=gnu11', '-fno-strict-aliasing',
        '-ffp-contract=off', '-frounding-math', '-Wall', '-Wextra', '-Werror',
        '-I' + str(ROOT / 'recomp'), str(ROOT / 'tools/tests/cluster_snapshot.c'),
        str(ROOT / 'recomp/kernel/xk_cluster_snapshot.c'),
        str(ROOT / 'recomp/kernel/xk_cluster_query.c'),
        '-pthread', '-lm', '-Wl,--wrap=calloc,--wrap=free', '-o', str(binary)]
    if a.sanitize != 'none':
        command += ['-fsanitize=' + ('address,undefined' if a.sanitize == 'address' else 'thread'),
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-no-pie']
    (a.out / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    subprocess.run(command, check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    (a.out / 'result.log').write_text(result.stdout + result.stderr)
    print(result.stdout + result.stderr, end='')
    result.check_returncode()


if __name__ == '__main__':
    main()
