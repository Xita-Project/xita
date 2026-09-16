#!/usr/bin/env python3
"""Real poll implementation and benchmark, with controlled directory/file I/O."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
sdk=Path(os.environ.get('VITASDK',str(Path.home()/'vitasdk')))
with tempfile.TemporaryDirectory(prefix='xita-diagnostic-poll-') as directory:
    for sanitized in (False,True):
        binary=Path(directory)/('sanitized' if sanitized else 'normal')
        subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-idirafter',str(sdk/'arm-vita-eabi/include'),*(['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'] if sanitized else []),str(root/'tools/tests/diagnostic_poll.c'),'-lm','-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
