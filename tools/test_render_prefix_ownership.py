#!/usr/bin/env python3
"""Private first-scene ownership model; no production integration or GPU claim.

Extract actual recorder, query lifecycle, constants allocation and RTT helpers.
Generate one resumable copy of actual replay. The model's GXM implementation
queues pointer reads until explicit completion, so guest source mutation cannot
be hidden by an immediate-draw stub. Shader/pixel correctness is out of scope.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def generate(out):
    if not __debug__:
        raise RuntimeError("ownership prototype requires Python assertions")
    if sha(ROOT / "runtime/xv_d3d.c") != "b05327c84091d38417f09b1fa166c89582738d4e16f27d03a19464c9f5c06d3c":
        raise RuntimeError("retained recorder/replay source drift")
    s = (ROOT / "runtime/xv_d3d.c").read_text()

    def region(a, b):
        start = s.index(a)
        return s[start:s.index(b, start)]

    recorder = region("static cmdlist_t *cur_list(void) {", "static int blend_mode(void)")
    recorder += region("void xv_d3d_Clear(", "void xv_d3d_Swap(void)")
    complete = region("void xv_d3d_visibility_complete(", "#if XV_QUERY_PREFIX_PUBLISH")
    prepare = region("static void visibility_draw_state(", "int xv_d3d_has_visibility(")
    # Separate physical one-time initialization from public submitted serials.
    # The removed loop is emitted unchanged as a helper called only at Present.
    a = prepare.index("    uint32_t submitted_us=")
    b = prepare.index("    static int attempted;", a)
    submit = prepare[a:b]
    prepare = prepare[:a] + prepare[b:]
    prepare = prepare.replace("void xv_d3d_visibility_prepare(", "static void visibility_physical_prepare(")
    prepare = prepare.replace("unsigned w, unsigned h)\n", "unsigned w, unsigned h, unsigned sealed_visibility)\n")
    prepare = prepare.replace("if (!l->nvisibility) return;", "if (!sealed_visibility) return;")
    prepare += "\nstatic void visibility_public_submit(uint32_t frame)\n{\n    cmdlist_t *l=g_lists[frame%XV_NUM_LISTS];\n" + submit + "}\n"

    rt = region("static int rt_formats(", "void xv_d3d_SetRenderTarget(")
    replay = region("int xv_d3d_render_targets(", "/* The clear quad")
    assert replay.count("XV_DS_SETUP(l, depth_tail_readonly);") == 1
    candidate = replay.replace("int xv_d3d_render_targets(", "static int prefix_replay_step(")
    candidate = candidate.replace("int depth_tail_readonly)\n", "int depth_tail_readonly, prefix_cursor *p, int early)\n")
    candidate = candidate.replace("SceGxmDepthStencilSurface bd = *depth;", "SceGxmDepthStencilSurface bd = p->started ? p->bd : *depth;")
    candidate = candidate.replace("XV_DS_SETUP(l, depth_tail_readonly);", "int ds_enabled=1, ds_valid=early?1:ds_list_valid(l), ds_stored=p->stored, ds_tail=!!depth_tail_readonly; (void)ds_enabled;")
    candidate = candidate.replace("unsigned clear_slot = 0, current = 0xff;", "unsigned clear_slot=p->clear, current=p->current;")
    candidate = candidate.replace("int open = 0;", "int open=0; /* A prefix is published only at an existing closed scene. */")
    candidate = candidate.replace("unsigned i = 0, u = 0;", "unsigned i=p->command, u=p->ui; p->started=1;")
    candidate = candidate.replace("l->ncmds", "p->commands").replace("l->nui", "p->uis")
    anchor = "                XV_DS_STORED(current);\n                open = 0;"
    assert candidate.count(anchor) == 1
    candidate = candidate.replace(anchor, anchor + "\n                if (early) {\n                    assert(i==p->cut && u==0);\n                    p->command=i; p->ui=u; p->clear=clear_slot;\n                    p->current=current; p->stored=ds_stored; p->bd=bd;\n                    p->ended=1; return 1;\n                }")
    # Full lists use the real depth proof; this fixture stubs only shader
    # reflection. First-prefix depth stores are forced identically by DS_FIRST.
    depth = (ROOT / "runtime/xv_depth_store.h").read_text()
    da = depth.index("static int ds_list_valid(")
    db = depth.index("void xv_d3d_depth_store_report(")
    depth = depth[da:db]
    constants = region("static const uint8_t *frame_constants_prepare(", "/* Every shipped Halo")
    # Prototype append-only update. No source prefix is re-read/copied.
    constants = constants.replace("frame_constants_prepare(const cmdlist_t *l, uint32_t frame)", "frame_constants_prepare(const cmdlist_t *l, uint32_t frame, unsigned nconsts)")
    constants = constants.replace("l->nconsts", "nconsts")
    constants = constants.replace("g_frame_constants[slot].frame == frame)", "g_frame_constants[slot].frame == frame && g_frame_constants[slot].copied == nconsts)")
    constants = constants.replace("    unsigned bytes = nconsts * sizeof(float);\n    memcpy(g_frame_constants[slot].memory, l->consts, bytes);\n    xv_gpu_flush_pump(g_frame_constants[slot].memory, bytes);", "    unsigned first=g_frame_constants[slot].ready && g_frame_constants[slot].frame==frame ? g_frame_constants[slot].copied : 0;\n    assert(nconsts>=first);\n    unsigned bytes=(nconsts-first)*sizeof(float);\n    if(bytes) { memcpy(g_frame_constants[slot].memory+first*sizeof(float),l->consts+first,bytes);\n        xv_gpu_flush_pump(g_frame_constants[slot].memory+first*sizeof(float),bytes); }\n    g_frame_constants[slot].copied=nconsts;")
    binding=region("static int frame_constants_raw_layout(", "/* End frame-owned vertex constants. */")
    binding=binding.replace("const xv_vshader_t *vs, uint32_t frame)\n", "const xv_vshader_t *vs, uint32_t frame, unsigned nconsts)\n")
    binding=binding.replace("l->nconsts", "nconsts").replace("frame_constants_prepare(l, frame)", "frame_constants_prepare(l, frame, nconsts)")
    constants += binding
    for name, body in {"recorder.inc": recorder, "visibility.inc": prepare+complete,
                       "rt.inc": rt, "replay.inc": replay, "prefix_replay.inc": candidate,
                       "depth.inc": depth, "constants.inc": constants}.items():
        (out / name).write_text(body)
    all_code = recorder + prepare + complete + rt + replay + constants
    constants = sorted(set(re.findall(r"\bSCE_[A-Z0-9_]+", all_code)))
    (out / "constants.h").write_text("\n".join(f"#define {v} {i+1}" for i, v in enumerate(constants)))
    return {name: sha(out / name) for name in sorted(p.name for p in out.glob("*.inc"))}


def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--out", type=Path, required=True)
    args=ap.parse_args(); args.out.mkdir(parents=True, exist_ok=True)
    generated=generate(args.out)
    exe=args.out/"ownership"
    command=["cc","-std=c11","-O1","-g","-Wall","-Wextra","-Werror",
             "-Wno-unused-function","-Wno-unused-parameter","-Wno-address",
             "-Wno-misleading-indentation","-fsanitize=address,undefined",
             "-fno-omit-frame-pointer","-no-pie","-I"+str(args.out),
             str(ROOT/"tools/tests/render_prefix_ownership.c"),"-o",str(exe)]
    with (args.out/"compile.log").open("w") as log:
        subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,check=True)
    proc=subprocess.run([str(exe)],text=True,capture_output=True,
                        env={**os.environ,"XV_RT_QUEUE":"1","XV_FRAME_CONSTANTS":"1","ASAN_OPTIONS":"detect_leaks=1"})
    (args.out/"run.log").write_text(proc.stdout+proc.stderr)
    negatives=[]
    if proc.returncode == 0:
        for name in ("EARLY_PUBLISH","ZERO_AT_PRESENT","RETIRE_PREFIX","CONST_READY"):
            bad=args.out/("bad-"+name.lower())
            badcommand=command[:-1]+[str(bad),"-DTEST_BAD_"+name]
            if name == "CONST_READY":
                p=args.out/"constants.inc";original=p.read_text()
                p.write_text(original.replace(" && g_frame_constants[slot].copied == nconsts", ""))
            try:
                subprocess.run(badcommand,capture_output=True,check=True)
            finally:
                if name == "CONST_READY":p.write_text(original)
            result=subprocess.run([str(bad)],capture_output=True,text=True,
                env={**os.environ,"XV_RT_QUEUE":"1","XV_FRAME_CONSTANTS":"1","ASAN_OPTIONS":"detect_leaks=1"})
            (args.out/("negative-"+name.lower()+".log")).write_text(result.stdout+result.stderr)
            if result.returncode == 0:raise RuntimeError("negative control escaped: "+name)
            negatives.append({"name":name,"returncode":result.returncode,"executable":sha(bad),"failure":result.stderr.strip().splitlines()[-1]})
    receipt={"base":"449aba89e42b7c4031e7985f72f50c5fe7c1fd4b","command":command,
             "returncode":proc.returncode,"generated":generated,"negative_controls":negatives,
             "sources":{p:sha(ROOT/p) for p in ["runtime/xv_d3d.c","runtime/xv_visibility.h",
                "runtime/xv_query_boundary.h","runtime/xv_depth_store.h","runtime/xv_frame_slots.h",
                "tools/test_render_prefix_ownership.py","tools/tests/render_prefix_ownership.c"]},
             "executable":sha(exe),"run_log":sha(args.out/"run.log")}
    (args.out/"receipt.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print(proc.stdout+proc.stderr,end=""); raise SystemExit(proc.returncode)


if __name__ == "__main__":
    main()
