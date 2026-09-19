#!/usr/bin/env python3
"""Host lifecycle tests of the actual RTT implementation, with mocked GXM calls.
No game data or GPU/emulator required. The mocks check ordering, not pixel formats.
"""
import os
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
src = (root / 'runtime/xv_d3d.c').read_text()
start = src.index('static cmdlist_t *cur_list(void);')
end = src.index('static int draw_scan_override', start)
# The vertex-capture completion callback now sits between the RTT helpers and
# UI recorder. It is not part of target lifetime/replay; do not pull it into
# this harness's deliberately smaller command model.
capture = src.index('static void captured_draw_complete(', start)
ui = src.index('int xv_d3d_record_ui(', capture)
implementation = src[start:capture] + src[ui:end]
start = src.index('int xv_d3d_has_render_targets(')
end = src.index('/* The clear quad', start)
implementation += src[start:end]
constants = sorted(set(re.findall(r'\bSCE_[A-Z0-9_]+', implementation)))
with tempfile.TemporaryDirectory(prefix='xita-rt-test-') as tmp:
    tmp = pathlib.Path(tmp)
    (tmp / 'constants.h').write_text('\n'.join(f'#define {c} {i+1}' for i, c in enumerate(constants)))
    (tmp / 'render_targets_under_test.inc').write_text(implementation)
    exe = tmp / 'test'
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-Werror', '-I', str(root / 'runtime'), '-I', str(tmp), str(root / 'tools/tests/render_targets_host.c'),
                    str(root / 'runtime/xv_render_profile.c'),
                    '-o', str(exe)], check=True)
    for queue in ('0', '1'):
        for scenes in ('1', '4', '8'):
            subprocess.run([str(exe)], check=True,
                           env={**os.environ, 'XV_DROP_RT': '0', 'XV_RT_QUEUE': queue, 'XV_RT_SCENES': scenes})
    subprocess.run([str(exe), '--drop'], check=True, env={**os.environ, 'XV_DROP_RT': '1'})
