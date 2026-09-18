#!/usr/bin/env python3
"""Run the production captured-preparation FIFO with real uploader/copy worker."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    sdk = Path(os.environ.get('VITASDK', Path.home() / 'vitasdk'))
    with tempfile.TemporaryDirectory(prefix='xita-vertex-capture-') as directory:
        for packed in (0, 1):
            binary = Path(directory) / f'capture-{packed}'
            command = ['cc', '-std=gnu11', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                       '-Wno-unused-parameter', '-Wno-misleading-indentation',
                       '-DXV_VERTEX_RESIDENT_REFERENCES_DEFAULT=1',
                       f'-DXV_PACKED_VERTEX_LAYOUT={packed}', '-I' + str(ROOT / 'runtime'),
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
