#!/usr/bin/env python3
"""Check exact query history publication and Halo's deferred-read lifetime."""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='xita-flare-query-overlap-') as directory:
    for name in ('visibility_history', 'flare_query_overlap'):
        output = Path(directory) / name
        command = [os.environ.get('CC', 'cc'), '-O2', '-g', '-std=gnu11',
                   '-fno-strict-aliasing', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-missing-field-initializers',
                   '-I' + str(root), '-I' + str(root / 'runtime'),
                   str(root / 'tools/tests' / (name + '.c')), '-pthread', '-lm', '-o', str(output)]
        if args.sanitize:
            command[1:1] = ['-fsanitize=address,undefined']
        subprocess.run(command, check=True)
        for value in ([None, '0', '1'] if name == 'flare_query_overlap' else [None]):
            env = dict(os.environ)
            for key in ('XV_FLARE_QUERY_OVERLAP', 'XV_FLARE_DEFER', 'XV_VIS_STALE',
                        'XV_VISIBILITY_EVENTS', 'XV_VISIBILITY_POLL_US', 'XV_VISIBILITY_BACKOFF'):
                env.pop(key, None)
            if value is not None:
                env['XV_FLARE_QUERY_OVERLAP'] = value
            subprocess.run([str(output)], env=env, check=True)
