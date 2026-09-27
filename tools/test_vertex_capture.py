#!/usr/bin/env python3
"""Run the production captured-preparation FIFO with real uploader/copy worker."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--jobs', type=int, choices=(32, 64, 128), default=32)
    parser.add_argument('--publish-batch', type=int, choices=(1, 8), default=1)
    args = parser.parse_args()
    sdk = Path(os.environ.get('VITASDK', Path.home() / 'vitasdk'))
    with tempfile.TemporaryDirectory(prefix='xita-vertex-capture-') as directory:
        for notify in (0, 1):
            for packed, compact, reuse, persistent, ready in ((0, 0, 0, 0, 0), (1, 0, 0, 0, 0), (1, 1, 0, 0, 0),
                                           (0, 0, 1, 0, 0), (1, 0, 1, 0, 0), (1, 1, 1, 0, 0),
                                           (0, 0, 0, 1, 0), (1, 1, 1, 1, 0),
                                           (0, 0, 1, 0, 1), (1, 0, 1, 0, 1),
                                           (1, 1, 1, 0, 1), (1, 1, 1, 1, 1)):
                binary = Path(directory) / f'capture-{packed}-{compact}-{reuse}-{persistent}-{ready}-{notify}'
                command = ['cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                           '-Wno-unused-parameter', '-Wno-misleading-indentation',
                           '-DXV_VERTEX_RESIDENT_REFERENCES_DEFAULT=1',
                           f'-DXV_CAPTURE_JOBS={args.jobs}',
                           f'-DXV_CAPTURE_PUBLISH_BATCH={args.publish_batch}',
                           f'-DXV_PACKED_VERTEX_LAYOUT={packed}', f'-DXV_VERTEX_CAPTURE_PACKED={compact}',
                           f'-DXV_VERTEX_CAPTURE_REUSE={reuse}',
                           f'-DXV_CAPTURE_TRUST_TAGS={reuse}',
                           f'-DXV_VERTEX_PERSISTENT={persistent}',
                           f'-DXV_VERTEX_CAPTURE_READY={ready}',
                           f'-DXV_VERTEX_CAPTURE_NOTIFY={notify}',
                           '-I' + str(ROOT / 'runtime'),
                           '-idirafter', str(sdk / 'arm-vita-eabi/include')]
                if os.environ.get('SANITIZE'):
                    command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
                if os.environ.get('THREAD_SANITIZE'):
                    # GCC cannot instrument the existing GPU-device fences. The
                    # queue's release/acquire counters and host semaphore/event
                    # synchronization remain instrumented.
                    command += ['-fsanitize=thread', '-fno-omit-frame-pointer', '-Wno-error=tsan']
                subprocess.run(command + [str(ROOT / 'tools/tests/vertex_capture.c'), '-pthread', '-o', str(binary)], check=True)
                subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == '__main__':
    main()
