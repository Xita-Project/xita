#!/usr/bin/env python3
"""Run a bounded composition extension of the real object-worker fixture.

No production files change. The existing fixture's native adapters and build
extraction are retained; only its hierarchy callback is replaced with a complete
region comparison and joined owner-side palette consumption.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def replace_once(text,old,new):
    if text.count(old)!=1:raise RuntimeError('Fixture source boundary changed: '+old[:80])
    return text.replace(old,new)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--regions',type=Path,required=True)
    ap.add_argument('--output-dir',type=Path,required=True)
    a=ap.parse_args();a.output_dir.mkdir(parents=True,exist_ok=True)
    original=(ROOT/'tools/tests/object_jobs.c').read_text()
    start=original.index('static void hierarchy_job(');end=original.index('\n#endif',start)
    source=original[:start]+(ROOT/'tools/tests/model_batch_workers.inc').read_text()+original[end:]
    source=replace_once(source,'xv_model_hierarchy_override(1);','xv_model_hierarchy_override(1);\n    xv_model_palette_override(1);')
    source=replace_once(source,'xv_object_jobs_join();assert(!active);','xv_object_jobs_join();assert(!active);\n        composition_owner_palette(round);')
    source=replace_once(source,'xv_model_hierarchy_override(0);','assert(composition_consumed==600);\n    puts("PASS: 600 complete hierarchy regions and joined palette consumers; exact context/memory/FP");\n    xv_model_palette_report(3);\n    xv_model_hierarchy_override(0);')
    fixture=a.output_dir/'object_jobs_composition.c';fixture.write_text(source)
    # Reuse only the original production-adapter extraction/build block. Avoid
    # rerunning unrelated default/abort/remote suites from the original driver.
    driver=(ROOT/'tools/test_object_jobs.py').read_text().split('    for timed in ("0", "1"):')[0]
    driver=replace_once(driver,'root=Path(__file__).resolve().parents[1]','root=Path('+repr(str(ROOT))+')')
    driver=replace_once(driver,"str(root/'tools/tests/object_jobs.c')",repr(str(fixture)))
    driver=replace_once(driver,"'-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_MODEL_HIERARCHY'","'-DXV_EXPERIMENTAL_OBJECT_JOBS','-DXV_NATIVE_MODEL_HIERARCHY','-DXV_NATIVE_MODEL_PALETTE'")
    driver=replace_once(driver,"str(root/'recomp/kernel/xk_hierarchy.c'),","str(root/'recomp/kernel/xk_hierarchy.c'),str(root/'recomp/kernel/xk_palette.c'),"+repr(str(a.regions))+",")
    driver += '''    receipts=[]
    for private in ("0","1"):
        for guards in ("0","1"):
            env=dict(os.environ,XV_OBJECT_JOB_WORKERS="2",XV_OBJECT_PRIVATE_MATH=private,XV_OBJECT_LOCK_PROFILE=guards,XV_OBJECT_TIMED_WAIT=guards)
            run=subprocess.run([str(binary)],env=env,check=True,timeout=60,capture_output=True,text=True)
            label="private-"+private+"-profile-"+guards
            (DEST/(label+".stdout")).write_text(run.stdout)
            (DEST/(label+".stderr")).write_text(run.stderr)
            assert "600 complete hierarchy regions" in run.stdout
            assert re.findall(r"\\[model-palette\\] 3 frames batches (\\d+) matrices (\\d+)",run.stderr)==[("600","4800")]
            assert re.findall(r"\\[model-hierarchy\\] 3 frames batches (\\d+) child nodes (\\d+)",run.stderr)==[("600","3600"),("0","0")]
            receipts.append(dict(private=private,profile=guards,regions=600,palette=600))
            print("PASS",label,flush=True)
    (DEST/"result.json").write_text(json.dumps(dict(rows=receipts,fixture_sha256=hashlib.sha256(FIXTURE.read_bytes()).hexdigest(),limitations="host floating point; arithmetic/FPSCR separately checked with exact retained ARM objects"),indent=2)+"\\n")
'''
    driver='import json,hashlib\nDEST=Path_PLACEHOLDER\nFIXTURE=FIXTURE_PLACEHOLDER\n'+driver
    driver=driver.replace('DEST=Path_PLACEHOLDER','from pathlib import Path\nDEST=Path('+repr(str(a.output_dir))+')').replace('FIXTURE=FIXTURE_PLACEHOLDER','FIXTURE=Path('+repr(str(fixture))+')')
    runner=a.output_dir/'run-worker-fixture.py';runner.write_text(driver)
    env=dict(os.environ,OBJECT_JOB_TEST_FLAGS='-DXV_OBJECT_QUAT_EXPERIMENT -DXV_OBJECT_HOLD_PROFILE -fsanitize=address,undefined -fno-omit-frame-pointer')
    subprocess.run([sys.executable,str(runner)],env=env,check=True)

if __name__=='__main__':main()
