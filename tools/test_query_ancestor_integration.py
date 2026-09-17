#!/usr/bin/env python3
"""Qualify the query-only build wiring against already-qualified ARM objects.

Copies a retained eighteen-path build to new private output, preserves its flags,
and changes only the ordered scalar ancestor selector. No new gameplay/semantic suite.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from elftools.elf.elffile import ELFFile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from tools import gen_native_query_fusion as generator
from tools import query_ancestor_scalar as ancestor

FEATURE='XV_QUERY_ANCESTOR_SCALAR'
INPUTS=('Makefile','tools/gen_native_query_fusion.py','tools/query_ancestor_scalar.py')
def sha(data):return hashlib.sha256(data).hexdigest()
def identity(path):
    with path.open('rb') as stream:
        elf=ELFFile(stream);sections={};relocations={}
        for s in elf.iter_sections():
            if s['sh_flags']&2:
                sections[s.name]=dict(size=s['sh_size'],type=s['sh_type'],flags=s['sh_flags'],
                    sha256=None if s['sh_type']=='SHT_NOBITS' else sha(s.data()))
            if s['sh_type'] not in ('SHT_REL','SHT_RELA'):continue
            if not (elf.get_section(s['sh_info'])['sh_flags']&2):continue
            symbols=elf.get_section(s['sh_link']);rows=[]
            for r in s.iter_relocations():
                symbol=symbols.get_symbol(r['r_info_sym'])
                name=symbol.name
                if symbol['st_info']['type']=='STT_SECTION':name=elf.get_section(symbol['st_shndx']).name
                rows.append([r['r_offset'],r['r_info_type'],name,r.entry.get('r_addend')])
            relocations[s.name]=rows
        imports=sorted(s.name for s in elf.get_section_by_name('.symtab').iter_symbols()
                       if s['st_shndx']=='SHN_UNDEF' and s.name)
        return dict(sections=sections,relocations=relocations,imports=imports)
def frames(path):
    return {line.split('\t')[0].rsplit(':',1)[1]:line.split('\t')[1:]
            for line in path.read_text().splitlines()}

def main():
    if not __debug__:raise SystemExit('Refusing optimized Python')
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('retained-build','build-command','qualified-dir','out'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arm-cc',type=Path,default=Path('/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-gcc'))
    a=p.parse_args();out=a.out.resolve();retained=a.retained_build.resolve()
    if out.is_relative_to(ROOT):p.error('owned output must remain outside source worktree')
    out.mkdir(parents=True,exist_ok=False);stage=out/'build'
    subprocess.run(['cp','-a','--reflink=auto',str(retained),str(stage)],check=True)
    for name in INPUTS:shutil.copy2(ROOT/name,stage/name)
    units=stage/'recomp';build=stage/'build/recomp'
    kept_names=['recomp/'+n for n in ('code_000.c','code_013.c','code_016.c','code_028.c','solver_fusion.c','solver_primitives.h')]
    kept_names += [str(n.relative_to(stage)) for n in build.rglob('*.o') if n.name!='query_fusion.o']
    kept={n:sha((stage/n).read_bytes()) for n in kept_names}
    old_source=(units/'query_fusion.c').read_bytes();old_object=(build/'query_fusion.o').read_bytes()
    qualified=identity(a.qualified_dir/'inline.o');old_identity=identity(build/'query_fusion.o')
    assert old_identity==identity(a.qualified_dir/'baseline.o')
    assert old_identity['sections']['.text']['sha256']==ancestor.RETAINED_TEXT_SHA256
    base=json.loads(a.build_command.read_text())['command'][:-1]
    assert all(x+'=1' in base for x in ('XV_QUERY_F32_INLINE','XV_NATIVE_SOLVER_FUSION',
        'XV_QUERY_SEMANTIC_LEAF','XV_QUERY_MEMBERSHIP_SCALAR','XV_QUERY_PREFIX_PUBLISH',
        'XV_TEXTURE_STATE_CACHE_DEFAULT','XV_DEPTH_PREPARE_DEFAULT'))
    # Keep every feature flag in the actual eighteen-path package command.
    base=[x for x in base if not x.startswith(('CC=','PREFIX=','VITASDK=','XBE=','XBE_JSON=','PYTHON=',FEATURE+'='))]
    cc=str(a.arm_cc)
    base+=['CC='+cc+' -fstack-usage','PREFIX='+cc.removesuffix('-gcc'),
           'VITASDK='+str(a.arm_cc.parent.parent),'PYTHON='+sys.executable,
           'XBE='+str(retained/'haloce/default.xbe'),
           'XBE_JSON='+str(retained/'local/halo_ce_3925/game_manifest.json')]
    # Caller/solver sources and all other retained objects are checked byte-for-byte.
    # Their unchanged generation does not justify repeated large caller compiles.
    targets=['build/recomp/query_fusion.o']
    rows=[]
    def stable():
        for n,expected in kept.items():assert sha((stage/n).read_bytes())==expected,n
    def run(label,mode=None):
        command=base+([] if mode is None else [FEATURE+'='+str(mode)])+targets
        proc=subprocess.run(command,cwd=stage,capture_output=True,text=True)
        (out/(label+'.log')).write_text(proc.stdout+proc.stderr)
        assert not proc.returncode,(label,proc.stdout,proc.stderr)
        compiles=[s for s in proc.stdout.splitlines() if ' -c recomp/' in s]
        actual=identity(build/'query_fusion.o')
        assert actual==(qualified if mode else old_identity),label
        stable()
        assert (build/'query-ancestor.config').read_text()==str(int(bool(mode)))+'\n'
        for line in compiles:
            assert ('-D'+FEATURE+'=1' in line)==bool(mode)
        row=dict(label=label,command=command,compiles=compiles,
            query_sha256=sha((build/'query_fusion.o').read_bytes()),query=actual)
        rows.append(row);(out/'commands.json').write_text(json.dumps(rows,indent=2)+'\n')
        print('PASS',label,'compiles',len(compiles),flush=True)
        return row
    run('default');assert (units/'query_fusion.c').read_bytes()==old_source
    assert (build/'query_fusion.o').read_bytes()==old_object
    assert not run('explicit-off-noop',0)['compiles']
    run('on',1)
    assert frames(build/'query_fusion.su')==frames(a.qualified_dir/'inline.su')
    shutil.copy2(units/'query_fusion.c',out/'on-query_fusion.c')
    for suffix in ('.o','.su','.d'):shutil.copy2(build/('query_fusion'+suffix),out/('on'+suffix))
    assert not run('on-noop',1)['compiles']
    (units/'query_fusion.c').unlink();run('missing-query',1)
    assert not run('repaired-noop',1)['compiles']
    module=stage/'tools/query_ancestor_scalar.py';saved=module.read_bytes()
    module.write_bytes(saved+b'\n# authored input dependency probe\n')
    run('authored-transform-change',1)
    assert not run('authored-noop',1)['compiles']
    module.write_bytes(saved);run('authored-transform-restore',1)
    assert not run('restored-noop',1)['compiles']
    run('off',0)
    assert (units/'query_fusion.c').read_bytes()==old_source
    assert (build/'query_fusion.o').read_bytes()==old_object
    assert not run('off-noop',0)['compiles']

    rejected=[]
    for value in ('','2','-1','0 1'):
        proc=subprocess.run(base+[FEATURE+'='+value,*targets],cwd=stage,capture_output=True,text=True)
        assert proc.returncode and FEATURE in proc.stderr,value
        rejected.append('Make-selector-'+repr(value))
    for settings in ([FEATURE+'=1','XV_QUERY_MEMBERSHIP_SCALAR=0'],
                     [FEATURE+'=1','XV_QUERY_SEMANTIC_LEAF=0'],
                     [FEATURE+'=1','XV_QUERY_F32_INLINE=0'],
                     [FEATURE+'=1','XV_NATIVE_QUERY_FUSION=0']):
        proc=subprocess.run(base+settings+targets,cwd=stage,capture_output=True,text=True)
        assert proc.returncode and 'requires' in proc.stderr,settings
        rejected.append('Make-prerequisite-'+settings[-1])
    # Reject drift before publication with the actual generator, no synthetic substitute.
    paths=[units/n for n in ('code_028.c','query_fusion.c','solver_fusion.c','solver_primitives.h','query_semantic_leaf.h')]
    paths+=list((units/'query_f32_primitives').rglob('*.h'))
    receipt=build/'query-fusion.generated.json';paths.append(receipt)
    before={n:n.read_bytes() for n in paths}
    def generate(mode=1,membership=1):
        return generator.generate(retained/'haloce/default.xbe',retained/'local/halo_ce_3925/game_manifest.json',
                                  units,receipt,1,1,1,membership,mode)
    def reject(label,operation):
        try:operation()
        except (ValueError,AssertionError,RuntimeError,FileNotFoundError):pass
        else:raise AssertionError('accepted '+label)
        assert all(n.read_bytes()==value for n,value in before.items()),label
        rejected.append(label)
    reject('generator-invalid-selector',lambda:generate(2))
    reject('generator-prerequisite',lambda:generate(1,0))
    original_hash=ancestor.ORIGINAL_SHA256
    ancestor.ORIGINAL_SHA256='0'*64
    reject('generator-exact-source-interval-drift',generate)
    ancestor.ORIGINAL_SHA256=original_hash
    # Reject external entry and missing composition markers using actual retained
    # query source, without installing any output or touching the qualified body.
    for label,source in (
            ('external-interior-entry',old_source.decode()+'\ngoto L_00087F80;\n'),
            ('missing-semantic-boundary',old_source.decode().replace('#include "query_semantic_leaf.h"',''))):
        reject('module-'+label,lambda source=source:ancestor.generate(source))
    module.rename(module.with_suffix('.held'))
    try:
        proc=subprocess.run(base+[FEATURE+'=1',*targets],cwd=stage,capture_output=True,text=True)
        assert proc.returncode and 'query_ancestor_scalar.py' in proc.stderr
        assert all(n.read_bytes()==value for n,value in before.items())
        rejected.append('Make-missing-authored-transform')
    finally:module.with_suffix('.held').rename(module)
    proc=subprocess.run([sys.executable,'-O',str(ROOT/'tools/gen_native_query_fusion.py'),'--help'],capture_output=True,text=True)
    assert proc.returncode and 'Refusing optimized Python' in proc.stderr;rejected.append('python-O')
    # A final ON receipt records the production module and exact authored input.
    run('final-on',1);assert not run('final-on-noop',1)['compiles']
    assert frames(build/'query_fusion.su')==frames(a.qualified_dir/'inline.su')
    result=dict(result='PASS',base_flags=base,rows=rows,
        qualified_allocated_sections_relocations_imports_identical=True,
        off_source_and_full_object_identical=True,unchanged_object_count=len(kept_names)-6,
        caller_solver_generic_source_and_objects_identical=True,kept_sha256=kept,
        generated_source_sha256=sha((units/'query_fusion.c').read_bytes()),
        authored_transform_sha256=sha((ROOT/'tools/query_ancestor_scalar.py').read_bytes()),
        frames=frames(build/'query_fusion.su'),failures_rejected=rejected,
        scalar_oracle_reused_by_object_identity=True,prior_fpscr_scope_unchanged=True,
        no_device_emulator_or_new_semantic_suite=True)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS query-only production integration; exact qualified object and OFF restoration',flush=True)

if __name__=='__main__':main()
