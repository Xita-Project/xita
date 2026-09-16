#!/usr/bin/env python3
"""Production sealed-list observer + actual replay, no game data or hardware."""
import os,pathlib,re,subprocess,tempfile
root=pathlib.Path(__file__).resolve().parents[1]
source=(root/'runtime/xv_d3d.c').read_text()
a=source.index('void xv_d3d_render(SceGxmContext *ctx, uint32_t frame)')
b=source.index('/* The clear quad',a)
implementation=source[a:b]
# Observe only after publication wakes exact-result consumers.
completion=source[source.index('void xv_d3d_visibility_complete('):source.index('static SceGxmBlendFactor alpha_blend_factor')]
assert completion.index('xk_os_scheduler_notify();')<completion.index('XV_VP_COMPLETE(l,frame)')
prepare=source[source.index('void xv_d3d_visibility_prepare('):source.index('int xv_d3d_has_visibility(')]
assert prepare.index('XV_VP_BEGIN(l,frame)')<prepare.index('memset(p,0,')
with tempfile.TemporaryDirectory(prefix='xita-placement-') as tmp:
    tmp=pathlib.Path(tmp);(tmp/'placement_replay.inc').write_text(implementation)
    outputs=[]
    for mode in (0,1):
        exe=tmp/f'placement-{mode}'
        command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                 '-Wno-unused-function','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                 '-fno-pie','-no-pie','-I',str(tmp)]
        if mode:command+=['-DXV_VISIBILITY_PLACEMENT']
        command+=[str(root/'tools/tests/visibility_placement.c'),str(root/'runtime/xv_render_profile.c'),'-o',str(exe)]
        subprocess.run(command,check=True)
        result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,
                              env={**os.environ,'XV_RT_QUEUE':'1','ASAN_OPTIONS':'detect_leaks=1'})
        print(result.stdout,end='');outputs.append(result.stdout)
    traces=[re.search(r'shared-replay-trace=(\w+)',s)[1] for s in outputs]
    assert traces[0]==traces[1],traces
print('OFF/ON replay traces match; observer owns only fixed private counters.')
