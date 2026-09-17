#!/usr/bin/env python3
"""Whole-query owned-XBE oracle for the optional native BSP traversal.

Private generated bodies never enter the source tree. The reference retains
the cumulative vertex, segment and BSP-distance helpers; the candidate adds
only the production traversal transform. Raw original bodies are also emitted
for independent boundary identity checks; they are not an executed third lane.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, struct, subprocess, sys
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from recompiler.core.hooks import NoGameHooks
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925 import collision_traversal as ct

ENTRIES = (0x88110, 0x87EA0, 0x87E10, 0x86F50, 0xB0CB0)
IMAGE = '4094e994243ddeae3f1b478bde6a7ee81498218ccd7c9d7bc2327db547d95aae'
PREAMBLE = '''#include "xv_x86rt.h"
extern unsigned ct_site;
extern void ct_observe_entry(xctx *, unsigned);
#undef X_G
#define X_G(a) ((void *)(xram_+xpt_[(uint32_t)(a)>>12]+((uint32_t)(a)&4095u)))
#undef X_IMG8
#undef X_IMG16
#undef X_IMG32
#define X_IMG8(a) (*(uint8_t *)(imgb_+(uint32_t)(a)))
#define X_IMG16(a) (*(xu16_u *)(imgb_+(uint32_t)(a)))
#define X_IMG32(a) (*(xu32_u *)(imgb_+(uint32_t)(a)))
'''


def observe_sites(body):
    site = 0
    rows = []
    for line in body.splitlines(True):
        m = re.search(r'/\* ([0-9A-F]{8}) ', line)
        if m: site = int(m[1], 16)
        if 'X_PREEMPT();' in line:
            line = line.replace('X_PREEMPT();', f'do {{ ct_site=0x{site:x}; X_PREEMPT(); }} while (0);')
        rows.append(line)
    return ''.join(rows)


def generate(a):
    if not __debug__:raise ValueError('Assertions required')
    image = r.Image(str(a.xbe), str(a.manifest))
    assert hashlib.sha256(image.data).hexdigest() == IMAGE and ct.matches(image)
    discovery = r.Discovery(image, {}, image.kernel_imports(), lambda *args: None)
    for address in ENTRIES: discovery.add_root(address)
    for address in ENTRIES:
        f = discovery.functions[address]; discovery.lift_function(f); discovery.split_blocks(f)
    raw = {}; baseline = {}; candidate = {}
    native_hook = ct.hook
    try:
        for address in ENTRIES:
            f = discovery.functions[address]
            raw[address] = r.Emitter(image, discovery, {}, image.kernel_imports(), 'unused', 1,
                                     hooks=NoGameHooks()).emit_function(f)
            ct.hook = lambda address, body: body
            baseline[address] = r.Emitter(image, discovery, {}, image.kernel_imports(), 'unused', 1,
                                          hooks=HaloHooks(image)).emit_function(f)
            ct.hook = native_hook
            candidate[address] = r.Emitter(image, discovery, {}, image.kernel_imports(), 'unused', 1,
                                           hooks=HaloHooks(image)).emit_function(f)
    finally:
        ct.hook = native_hook
    for address in (0x88110, 0x86F50, 0xB0CB0):
        assert baseline[address] == candidate[address], hex(address)
    # Actual compile-OFF output, including the retained BSP hook, must match.
    for address in ct.SPANS:
        pp=[]
        for name, bodies in [('baseline',baseline),('candidate',candidate)]:
            p=a.out/f'{name}-{address:x}-private.c'; p.write_text(PREAMBLE+bodies[address])
            pp.append(subprocess.check_output([os.environ.get('CC','cc'),'-E','-P','-I'+str(ROOT/'recomp'),str(p)]))
        assert pp[0] == pp[1], 'compile-OFF changed'
    read=image.bytes_at
    image.bytes_at=lambda addr,n: bytes(n) if addr in ct.SPANS else read(addr,n)
    assert not ct.matches(image)
    image.bytes_at=read
    for address in (0x87E1F,0x87EC1,0x87F80):
        assert HaloHooks(image).transform_body(address,raw[0x87E10]) == raw[0x87E10]
    text=PREAMBLE
    for prefix in ('original','candidate','raw'):
        text+=''.join(f'void {prefix}_{x:08X}(xctx *);\n' for x in ENTRIES)
    for prefix,bodies in [('original',baseline),('candidate',candidate),('raw',raw)]:
        for address,body in bodies.items():
            (a.out/f'{prefix}-{address:08X}-private.c').write_text(body+'\n')
            body=re.sub(r'\bf_([0-9A-F]{8})\b',prefix+r'_\1',body)
            if a.entry_observers:
                start=body.index(f'void {prefix}_{address:08X}(xctx *restrict c)')
                line='    uint8_t *const imgb_ = g_img_base; (void)imgb_;\n'
                at=body.index(line,start)+len(line)
                body=body[:at]+f'    ct_observe_entry(c, 0x{address:x});\n'+body[at:]
            # Retain private callback-site instrumentation identically in both
            # lanes. Production has no site writes or instrumentation calls.
            text+=(body if a.no_site_trace else observe_sites(body))+'\n'
    p=a.out/'oracle-private.c';p.write_text(text)
    (a.out/'oracle.json').write_text(json.dumps({'image_sha256':IMAGE,'spans':ct.SPANS,
        'raw_sha256':{hex(k):hashlib.sha256(v.encode()).hexdigest() for k,v in raw.items()},
        'compile_off_identical':True,'unchanged_visitors_and_wrapper':True},indent=2)+'\n')
    return p


def arm(a, source, flags, sources):
    from test_arm_cluster_runtime import RuntimeMachine, RAM, SIZE
    cc=os.environ.get('ARM_CC','/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc')
    flags += ['-DTEST_ARM','-mthumb','-mcpu=cortex-a9','-mfpu=neon']
    sources += [ROOT/'tools/tests/cluster_runtime_arm_imports.c']
    objects=[];commands=[]
    for i,s in enumerate(sources):
        obj=a.out/f'{i}.o';cmd=[cc,*flags,'-c',str(s),'-o',str(obj)]
        subprocess.run(cmd,check=True);objects.append(str(obj));commands.append(cmd)
    names=['arm_prepare','arm_original','arm_candidate','arm_context_ptr','layout',
           'ct_yields','ct_events','ct_seen','ct_original_pages','ct_alternate_pages','g_img_base']
    elf=a.out/'traversal.elf'
    cmd=[cc,*flags,*objects,'-nostdlib','-Wl,-Ttext=0x10000,-e,test_boot,--gc-sections,--wrap=xv_preempt,'+
         ','.join('--undefined='+n for n in names),'-lc','-lgcc','-o',str(elf)]
    subprocess.run(cmd,check=True);commands.append(cmd)
    (a.out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
    m=RuntimeMachine(elf,profile=True);m.imports={k:v for k,v in m.imports.items() if v!='__wrap_xv_preempt'}
    rows=[]
    specs=[(d,v,100000,0) for d in (1,16,64,128) for v in (4,4096|4|(1<<25))]
    specs += [(d,v,100000,0) for d in (1,3,8,16) for v in (0,3,4,8,12,15,2048,4096,4099)]
    specs += [(3,(mutation<<14)|v,1,0) for mutation in range(7) for v in (0,4,8,15,2048,4099)]
    specs += [(3,v|((top&7)<<8),100000,fpscr) for top in range(8) for v in (0,4,15,4099)
              for fpscr in (0x400000,0x800000,0xc00000,0x0300009f,0xf0000000)]
    if a.arm_limit:specs=specs[:a.arm_limit]
    def snapshot():
        state={'context':bytes(m.uc.mem_read(m.context,m.layout['size'])),
               'memory':bytes(m.uc.mem_read(RAM,SIZE))}
        for name,size in [('ct_yields',4),('ct_events',4),('ct_seen',4),
                          ('ct_original_pages',2048),('ct_alternate_pages',2048),('g_xpt',4)]:
            state[name]=bytes(m.uc.mem_read(m.symbols[name],size))
        return state
    for i,(d,v,b,fp) in enumerate(specs):
        m.call('arm_prepare',(d,v,b));original=m.call('arm_original',fpscr=fp);expected=snapshot()
        m.call('arm_prepare',(d,v,b));candidate=m.call('arm_candidate',fpscr=fp);actual=snapshot()
        checks={k:actual[k]==value for k,value in expected.items()};checks['fpscr']=original['fpscr']==candidate['fpscr']
        row={'depth':d,'variant':v,'budget':b,'fpscr':fp,'original':original,'candidate':candidate,'checks':checks,
             'yields':struct.unpack('<I',actual['ct_yields'])[0],'seen':struct.unpack('<I',actual['ct_seen'])[0]}
        rows.append(row)
        if not all(checks.values()):
            (a.out/'failure.json').write_text(json.dumps(row,indent=2)+'\n')
            for k,good in checks.items():
                if not good and k!='fpscr':
                    diffs=[(i,x,y) for i,(x,y) in enumerate(zip(expected[k],actual[k])) if x!=y][:32]
                    print(k,diffs,flush=True)
            raise AssertionError({k:v for k,v in row.items() if k not in ('original','candidate')})
        print('PASS ARM',i,d,v,b,fp,original['instructions'],candidate['instructions'],flush=True)
        (a.out/'result.json').write_text(json.dumps(rows,indent=2)+'\n')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('xbe','manifest','out'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--sanitize',action='store_true');p.add_argument('--emit-only',action='store_true')
    p.add_argument('--arm',action='store_true');p.add_argument('--arm-limit',type=int,default=0)
    p.add_argument('--no-site-trace',action='store_true',help='ARM cost: use exact production bodies without fixture backedge-site writes')
    p.add_argument('--entry-observers',action='store_true',help='Correctness: hash full context at each real wrapper/traversal/visitor/segment entry')
    p.add_argument('--cases',type=int,default=256);p.add_argument('--default',type=int,choices=(0,1),default=1)
    p.add_argument('--negative-control',choices=('no-yield-publication','captured-pop-mapping'))
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False);source=generate(a)
    if a.emit_only:return
    if a.negative_control:
        text=source.read_text()
        if a.negative_control=='no-yield-publication':
            old='CT_SAVE(); xv_preempt(c); CT_LOAD();';new='xv_preempt(c); CT_LOAD();'
        else:
            old='g_xram+g_xpt[ct_r4>>12]+(ct_r4&4095u)';new='xram_+xpt_[ct_r4>>12]+(ct_r4&4095u)'
        assert text.count(old)==2;text=text.replace(old,new);source.write_text(text)
    flags=['-O2','-g1','-std=gnu11','-fno-strict-aliasing','-ffunction-sections','-fdata-sections',
           '-DXV_NATIVE_COLLISION_TRAVERSAL',f'-DXV_NATIVE_COLLISION_TRAVERSAL_DEFAULT={a.default}',
           '-DXV_NATIVE_COLLISION_VERTICES','-DXV_NATIVE_COLLISION_VERTICES_DEFAULT=1',
           '-DXV_NATIVE_SEGMENT_SPHERE','-DXV_NATIVE_SEGMENT_SPHERE_DEFAULT=1','-DXV_NATIVE_BSP_SPHERE',
           '-I'+str(ROOT/'recomp'),f'-DCT_CASES={a.cases}']
    if a.sanitize:flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie']
    sources=[source,ROOT/'tools/tests/collision_traversal.c']+[ROOT/'recomp/kernel'/n for n in
             ('xk_collision_traversal_control.c','xk_collision_vertices_control.c','xk_segment_sphere_control.c','xk_geometry.c')]
    if a.arm:return arm(a,source,flags,sources)
    cmd=[os.environ.get('CC','cc'),*flags,*map(str,sources),'-Wl,--gc-sections,--wrap=xv_preempt','-lm','-pthread','-o',str(a.out/'test')]
    (a.out/'command.json').write_text(json.dumps(cmd,indent=2)+'\n');subprocess.run(cmd,check=True)
    result=subprocess.run([str(a.out/'test')],capture_output=True,text=True)
    (a.out/'run.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='');result.check_returncode()
if __name__=='__main__':main()
