#!/usr/bin/env python3
"""Check capture notification default and owning-object Make transitions."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    with tempfile.TemporaryDirectory(prefix='xita-capture-notify-build-') as directory:
        out = Path(directory)
        (out / 'Makefile').write_text((ROOT / 'Makefile').read_text())
        (out / 'runtime').mkdir()
        profile = out / 'games/halo_ce_3925/runtime.mk'
        profile.parent.mkdir(parents=True)
        profile.write_text((ROOT / 'games/halo_ce_3925/runtime.mk').read_text())
        for header in (ROOT / 'runtime').glob('*.h'):
            (out / 'runtime' / header.name).write_bytes(header.read_bytes())
        for name in ('xv_vertex_capture', 'xv_vertex_upload'):
            path = out / f'runtime/{name}.c'
            path.write_text('int capture_fixture;\n')
            os.utime(path, (1_000_000_000,) * 2)
        recorder = out / 'record.py'
        recorder.write_text('''import json,subprocess,sys
from pathlib import Path
args=sys.argv[1:]
with Path('commands.jsonl').open('a') as f:f.write(json.dumps(args)+'\\n')
subprocess.run(['cc',*[a for a in args if a != '-mthumb']],check=True)
''')
        base = ['make', '--no-print-directory', 'RECOMP=0', 'VITASDK='+str(out),
                'CC='+shlex.join([sys.executable, str(recorder)]),
                'build/runtime/xv_vertex_capture.o', 'build/runtime/xv_vertex_upload.o']
        previous = None
        for mode in (None, 0, 1, 1, 0, 0):
            (out / 'commands.jsonl').write_text('')
            args = base + ([] if mode is None else [f'XV_VERTEX_CAPTURE_NOTIFY={mode}'])
            result = subprocess.run(args, cwd=out, capture_output=True, text=True)
            assert not result.returncode, result.stdout+result.stderr
            commands = [json.loads(line) for line in (out / 'commands.jsonl').read_text().splitlines()]
            compiles = {c[c.index('-c')+1]: c for c in commands}
            effective = mode or 0
            want = {'runtime/xv_vertex_capture.c', 'runtime/xv_vertex_upload.c'} if previous is None else (
                {'runtime/xv_vertex_capture.c'} if previous != effective else set())
            assert set(compiles) == want, compiles
            for name, command in compiles.items():
                flags = [v for v in command if v.startswith('-DXV_VERTEX_CAPTURE_NOTIFY=')]
                assert flags == ([f'-DXV_VERTEX_CAPTURE_NOTIFY={effective}'] if name.endswith('capture.c') else [])
            previous = effective
        for bad in ('', '2', '-1', '0 1'):
            result = subprocess.run(base + ['XV_VERTEX_CAPTURE_NOTIFY='+bad], cwd=out, capture_output=True, text=True)
            assert result.returncode and 'XV_VERTEX_CAPTURE_NOTIFY must be 0 or 1' in result.stderr
        print('PASS: default OFF, OFF/ON/no-op/OFF transitions rebuild capture only; invalid flags rejected')


if __name__ == '__main__':
    if not __debug__:
        raise SystemExit('Run without Python -O')
    main()
