#!/usr/bin/env python3
"""Three-arm full context/memory/preemption oracle from generated owned inputs."""
from pathlib import Path
import argparse,json,os,shlex,subprocess,sys,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from games.halo_ce_3925.clip_region import hook

MAPPING='#undef X_G\n#define X_G(a) ((void *)(xram_ + xpt_[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu)))\n#undef X_IMG8\n#undef X_IMG16\n#undef X_IMG32\n#define X_IMG8(a) (*(uint8_t *)(imgb_ + (uint32_t)(a)))\n#define X_IMG16(a) (*(xu16_u *)(imgb_ + (uint32_t)(a)))\n#define X_IMG32(a) (*(xu32_u *)(imgb_ + (uint32_t)(a)))\n'


def prepare(out,no_markers=False):
    raw={int(k):v for k,v in json.loads((ROOT/'recomp/host/build/clip_region_original.json').read_text())['raw'].items()}
    # Match the real generated translation unit's mapping/image macros. Header
    # inline helpers retain their own original global mappings, as in production.
    mapping=MAPPING
    common='''#include "xv_x86rt.h"
#include "clip_region_observe.h"
#include "kernel/xk_object_jobs.h"
extern void f_0001D130(xctx*),f_000117B0(xctx*),f_000B71C0(xctx*);
extern int xv_math_polygon_clip(xctx*);
'''
    probe=raw[0x1D130].replace('f_0001D130(', 'raw_probe(')
    (out/'probe.c').write_text('#include "xv_x86rt.h"\n'+mapping+probe)
    ref=raw[0x117B0]+'\n'+raw[0xB71C0]
    ref=ref.replace('    f_0001D130(c);','    f_0001D130(c);\n    if(!xv_is_object_job(c)){if(xv_watch_n)xv_watch_leave(xv_cur_fn,0xB71C0,c); xv_cur_fn=0xB71C0;}')
    ref+='''
static void original_clip_call(xctx*c){xv_cur_fn=0xB71C0;int token=xv_object_math_lock();f_000B71C0(c);xv_object_math_unlock(&token);xv_cur_fn=0xB7F10;}
static void current_clip_call(xctx*c){xv_cur_fn=0xB71C0;if(!xv_math_polygon_clip(c))abort();xv_cur_fn=0xB7F10;}
'''
    wrapper=raw[0xB7F10].replace('    f_0001D130(c);','    f_0001D130(c);\n    if(xv_watch_n)xv_watch_leave(xv_cur_fn,0xB7F10,c); xv_cur_fn=0xB7F10;')
    ref+=wrapper.replace('f_000B7F10(', 'original_wrapper(').replace('f_000B71C0(c);','original_clip_call(c);')+'\n'
    ref+=wrapper.replace('f_000B7F10(', 'current_wrapper(').replace('f_000B71C0(c);','current_clip_call(c);')+'\n'
    # Harness supplies the production diagnostic build's call markers. The
    # control itself is compiled unchanged, including all OFF/admission checks.
    ref+=hook(wrapper).replace('xv_math_clip_region(c, 0)','xv_math_clip_region(c, 1)').replace('f_000B7F10(', 'fused_wrapper(').replace('f_000B71C0(c);','current_clip_call(c);')
    if no_markers:
        ref=ref.replace('xv_cur_fn=0xB71C0;', '').replace('xv_cur_fn=0xB7F10;', '')
        ref=ref.replace('xv_math_clip_region(c, 1)', 'xv_math_clip_region(c, 0)')
    (out/'reference.c').write_text(common+mapping+ref)


def run(out,sanitize=False,no_markers=False):
    prepare(out,no_markers)
    for park in (False,True):
        binary=out/('host'+('-park' if park else '')+('-asan' if sanitize else ''))
        command=[os.environ.get('CC','cc'),'-O2','-std=gnu11','-fno-strict-aliasing','-ffp-contract=off','-pthread','-ffunction-sections','-fdata-sections','-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_CLIP_REGION','-DCASES=4096','-DSTRONG_MUTATION','-I'+str(ROOT/'tools/tests'),'-I'+str(ROOT/'recomp')]
        if no_markers: command+=['-DCLIP_TEST_NO_MARKERS']
        if park: command+=['-DPARK_MUTATION']
        if sanitize: command+=['-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
        command+=shlex.split(os.environ.get('CLIP_REGION_CFLAGS',''))
        command +=[str(p) for p in (out/'reference.c',out/'probe.c',ROOT/'tools/tests/clip_region.c',ROOT/'recomp/kernel/xk_clip.c',ROOT/'recomp/kernel/xk_clip_region.c',ROOT/'recomp/kernel/xk_clip_region_control.c',ROOT/'recomp/xv_x86rt.c')]
        command+=['-Wl,--gc-sections,--wrap=x_str_movs,--wrap=xv_preempt','-lm','-o',str(binary)]
        subprocess.run(command,check=True)
        subprocess.run([str(binary)],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),timeout=240)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path);p.add_argument('--sanitize',action='store_true');p.add_argument('--no-markers',action='store_true');a=p.parse_args()
    if a.out: a.out.mkdir(parents=True,exist_ok=True);run(a.out.resolve(),a.sanitize,a.no_markers)
    else:
        with tempfile.TemporaryDirectory(prefix='xita-clip-region-') as d: run(Path(d),a.sanitize,a.no_markers)
