#!/usr/bin/env python3
"""Check query-only f32 integration with actual retained production objects.

Owned inputs stay in a new private directory. No gameplay or device access.
The qualified complete-query oracle is reused by exact object/source identity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools import gen_native_query_fusion as generator
from tools import query_f32_primitives as primitives
from elftools.elf.elffile import ELFFile


def sha(data): return hashlib.sha256(data).hexdigest()


def sections(path):
    with path.open('rb') as stream:
        elf = ELFFile(stream)
        return {s.name: sha(s.data()) for s in elf.iter_sections()
                if s['sh_flags'] & 2 and s['sh_type'] != 'SHT_NOBITS'}


def main():
    if not __debug__: raise SystemExit('Refusing optimized Python')
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('retained-build','build-command','qualified-dir','xbe','manifest','out'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arm-cc',type=Path,default=Path('/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc'))
    p.add_argument('--resume-checks',action='store_true',help='Resume publication checks after a saved complete production build matrix')
    a=p.parse_args(); out=a.out.resolve()
    if out.is_relative_to(ROOT):p.error('owned outputs must remain outside source worktree')
    out.mkdir(parents=True,exist_ok=a.resume_checks);stage=out/'build'
    if not a.resume_checks:
        subprocess.run(['cp','-a','--reflink=auto',str(a.retained_build),str(stage)],check=True)
    for name in ('Makefile','tools/gen_native_query_fusion.py','tools/query_f32_primitives.py','tools/query_semantic_leaf.py','tools/query_membership_scalar.py','tools/query_ancestor_scalar.py','tools/query_object_space.py','tools/query_world_run.py','tools/query_world_run.h'):
        shutil.copy2(ROOT/name,stage/name)
    # The stage contains the actual retained solver, caller and generic units.
    units=stage/'recomp'; build=stage/'build/recomp'
    source_names=('code_000.c','code_013.c','code_016.c','code_028.c','solver_fusion.c','solver_primitives.h')
    object_names=('code_000.o','code_013.o','code_016.o','code_028.o','solver_fusion.o')
    original_sources={n:(a.retained_build/'recomp'/n).read_bytes() for n in source_names}
    original_objects={n:(a.retained_build/'build/recomp'/n).read_bytes() for n in object_names}
    original_query=(a.retained_build/'recomp/query_fusion.c').read_bytes()
    original_query_object=(a.retained_build/'build/recomp/query_fusion.o').read_bytes()
    base=json.loads(a.build_command.read_text())['command'][:-1]
    base=[x for x in base if not x.startswith(('CC=','PREFIX=','VITASDK=','XBE=','XBE_JSON=','PYTHON=','XV_QUERY_F32_INLINE='))]
    cc=str(a.arm_cc)
    base+=['CC='+cc+' -fstack-usage','PREFIX='+cc.removesuffix('-gcc'),
           'VITASDK='+str(a.arm_cc.parent.parent),'PYTHON='+sys.executable,
           'XBE='+str(a.xbe.resolve()),'XBE_JSON='+str(a.manifest.resolve())]
    targets=['build/recomp/'+n for n in ('query_fusion.o','solver_fusion.o','code_028.o')]
    rows=json.loads((out/'commands.json').read_text()) if a.resume_checks else []
    def run(label,mode=None,profile=1):
        command=base+['XV_OBJECT_HOLD_PROFILE='+str(profile)]
        if mode is not None:command+=['XV_QUERY_F32_INLINE='+str(mode)]
        command+=targets if label == 'on' else targets[:1]
        proc=subprocess.run(command,cwd=stage,capture_output=True,text=True)
        (out/(label+'.log')).write_text(proc.stdout+proc.stderr)
        if proc.returncode:raise AssertionError((label,proc.stdout,proc.stderr))
        compiles=[line for line in proc.stdout.splitlines() if ' -c recomp/' in line]
        row=dict(label=label,command=command,compiles=compiles,
                 query_sections=sections(build/'query_fusion.o'),
                 query_sha256=sha((build/'query_fusion.o').read_bytes()))
        rows.append(row)
        (out/'commands.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('PASS Make',label,'compiles',len(compiles),flush=True)
        return row
    def kept():
        for name,data in original_sources.items():assert (units/name).read_bytes()==data,name
        for name,data in original_objects.items():assert (build/name).read_bytes()==data,name
    if not a.resume_checks:
        run('default')
        assert (units/'query_fusion.c').read_bytes()==original_query
        assert (build/'query_fusion.o').read_bytes()==original_query_object
        assert not run('explicit-off-noop',0)['compiles']
        kept()
        run('on',1)
        assert (units/'query_fusion.c').read_bytes()==(a.qualified_dir/'inline.c').read_bytes()
        assert (build/'query_fusion.o').read_bytes()==(a.qualified_dir/'inline.o').read_bytes()
        assert (units/'query_f32_primitives/xv_x86rt.h').read_bytes()==(a.qualified_dir/'query_f32_primitives.h').read_bytes()
        for name in ('query_fusion.c',):shutil.copy2(units/name,out/('on-'+name))
        for suffix in ('.o','.su','.d'):shutil.copy2(build/('query_fusion'+suffix),out/('on'+suffix))
        kept();assert not run('on-noop',1)['compiles']
        for name in ('xv_x86rt.h','kernel/xk_object_jobs.h'):
            (units/'query_f32_primitives'/name).unlink()
            run('missing-'+Path(name).stem,1)
            assert sections(build/'query_fusion.o')==sections(a.qualified_dir/'inline.o')
            assert not run('repaired-'+Path(name).stem,1)['compiles']
        header=units/'xv_phase.h';saved=header.read_bytes()
        header.write_bytes(saved+b'\n/* canonical input dependency probe */\n')
        run('canonical-header-change',1)
        assert (units/'query_f32_primitives/xv_phase.h').read_bytes()==header.read_bytes()
        assert sections(build/'query_fusion.o')==sections(a.qualified_dir/'inline.o')
        assert not run('canonical-header-noop',1)['compiles']
        header.write_bytes(saved);run('canonical-header-restore',1)
        assert not (units/'query_f32_primitives/query_f32_primitives').exists()
        assert set(str(f.relative_to(units/'query_f32_primitives')) for f in (units/'query_f32_primitives').rglob('*') if f.is_file())==set(primitives.HEADERS)
        run('profile-off',1,0);assert not run('profile-off-noop',1,0)['compiles']
        run('profile-on',1,1)
        assert sections(build/'query_fusion.o')==sections(a.qualified_dir/'inline.o')
        kept()
    else:
        assert any(row['label']=='profile-on' for row in rows), 'incomplete production build matrix'
        run('final-generator-on',1)
        assert (build/'query_fusion.o').read_bytes()==(a.qualified_dir/'inline.o').read_bytes()
        kept()
    # Preprocessor evidence uses the actual production compiler invocation.
    on_command=next(x for x in rows if x['label']=='on')['compiles']
    import shlex
    command=shlex.split(next(x for x in on_command if ' -c recomp/query_fusion.c ' in x))
    command[command.index('-c')]='-E';command.insert(command.index('-E'),'-P')
    command[command.index('-o')+1]=str(out/'query-preprocessed.c')
    subprocess.run(command,cwd=stage,check=True)
    preprocessed=(out/'query-preprocessed.c').read_text()
    bindings={}
    for name in ('x_guest_read','x_guest_write'):
        start=re.search(r'static inline[^\n]*\b'+name+r'\(',preprocessed).start()
        end=preprocessed.index('\n}',start)+2;body=preprocessed[start:end]
        assert 'g_xram' in body and 'g_xpt' in body and 'xram_' not in body and 'xpt_' not in body,name
        bindings[name]=body
    run('off',0)
    assert (units/'query_fusion.c').read_bytes()==original_query
    assert (build/'query_fusion.o').read_bytes()==original_query_object
    assert not run('off-noop',0)['compiles'];kept()

    # These are actual generator validation failures, never the graph fixture.
    failures=[]
    def reject(label,operation):
        paths=[units/n for n in source_names]+[units/'query_fusion.c']+list((units/'query_f32_primitives').rglob('*.h'))
        before={x:x.read_bytes() for x in paths}
        receipt=build/'query-fusion.generated.json';stamp=receipt.read_bytes()
        try:operation()
        except (ValueError,AssertionError,RuntimeError):failures.append(label)
        else:raise AssertionError('accepted '+label)
        assert all(x.read_bytes()==data for x,data in before.items()),label
        assert receipt.read_bytes()==stamp,label
    def generate(mode=1):return generator.generate(a.xbe,a.manifest,units,build/'query-fusion.generated.json',1,mode)
    primitive=units/'xv_x86rt.h';saved=primitive.read_bytes()
    primitive.write_bytes(saved.replace(b'static inline double x87_load_f32(',b'static double x87_load_f32(',1))
    reject('changed-helper-declaration',generate);primitive.write_bytes(saved)
    primitive.write_bytes(saved.replace(b'#define X_G(a)      ((void *)(g_xram + g_xpt[',b'#define X_G(a)      ((void *)(other_arena + g_xpt[',1))
    reject('changed-global-root-expression',generate);primitive.write_bytes(saved)
    header=units/'kernel/xk_light_census.h';saved=header.read_bytes()
    header.write_bytes(saved+b'\n#include "unreviewed.h"\n')
    reject('unreviewed-transitive-header',generate);header.write_bytes(saved)
    reject('invalid-selector',lambda:generate(2))
    proc=subprocess.run([sys.executable,'-O',str(ROOT/'tools/gen_native_query_fusion.py'),'--help'],capture_output=True,text=True)
    assert proc.returncode and 'Refusing optimized Python' in proc.stderr
    failures.append('python-O')
    # Revalidate the final canonical inputs after the negative controls.
    run('post-validation-on',1)
    assert (build/'query_fusion.o').read_bytes()==(a.qualified_dir/'inline.o').read_bytes()
    run('post-validation-off',0)
    assert (build/'query_fusion.o').read_bytes()==original_query_object
    kept()
    # Verify Make/header inventories cannot silently diverge.
    make=(ROOT/'Makefile').read_text().replace('\\\n',' ')
    names=re.search(r'^QUERY_F32_HEADERS := (.*)$',make,re.M)[1].split()
    assert set(names)==set(primitives.HEADERS)
    (out/'result.json').write_text(json.dumps(dict(result='PASS',rows=rows,
        sources_and_caller_solver_generic_objects_identical=True,
        off_query_source_and_object_identical=True,qualified_inline_alloc_sections_identical=True,qualified_inline_full_object_identical=(out/'on.o').read_bytes()==(a.qualified_dir/'inline.o').read_bytes(),
        canonical_headers=list(primitives.HEADERS),global_memory_bindings=bindings,
        failures_rejected=failures,stack_usage=(out/'on.su').read_text(),
        no_hardware_or_new_semantic_oracle=True),indent=2)+'\n')
    print('PASS exact production query integration, retained objects, header recovery and strict failures')


if __name__=='__main__':main()
