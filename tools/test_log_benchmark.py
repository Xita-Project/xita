#!/usr/bin/env python3
"""Physical logger comparison: owner admission, pacing and failed boundaries."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='xita-log-benchmark-') as directory:
 for absent in (False,True):
  binary=Path(directory)/('absent' if absent else 'present')
  subprocess.run([os.environ.get('CC','cc'),'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie',*(['-DTEST_NO_LOG_API'] if absent else []),str(root/'tools/tests/log_benchmark.c'),'-lm','-o',str(binary)],check=True)
  subprocess.run([str(binary)],check=True)
