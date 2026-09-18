#!/usr/bin/env python3
"""Emit the qualified ordered portal loop for an exact retained Halo CE stage.

Owned inputs and generated bodies stay outside the repository. The supplied
stage is read-only; install only the returned primary body and private helpers.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


def generate(xbe, manifest, symbols, stage, out):
    if not __debug__:
        raise RuntimeError("Run without Python -O: identity checks require assertions")
    destination = out.resolve()
    for protected in (ROOT.resolve(), stage.resolve()):
        if destination == protected or protected in destination.parents:
            raise ValueError("Generated output must be outside the source repository and retained stage")
    from recompiler import xita_recomp as r
    from games.halo_ce_3925.discovery import HaloDiscovery
    from recompiler.core.profile import load_profile
    from games.halo_ce_3925.hooks import HaloHooks
    from tools.visibility_portal_loop import FLAG, transform

    img = r.Image(str(xbe), str(manifest))
    profile = load_profile("halo_ce_3925")
    data = symbols.read_bytes()
    profile.validate_image(img)
    profile.validate_symbols(data)
    parsed = json.loads(data)
    lift = r.DEFAULT_LIFT | set(profile.lift)
    hle = {s["address"]: s for s in parsed if s["kind"] == "FUN"
           and s["name"] not in lift
           and (s["lib"] in r.HLE_LIBS or s["name"] in r.HLE_KEEP)}
    hle.update(profile.overrides)
    variables = {s["name"]: s["address"] for s in parsed if s["kind"] == "VAR"}
    variables.update(profile.variables)
    hooks = HaloHooks(img)
    assert hooks.enabled
    d = HaloDiscovery(img, hle, img.kernel_imports(), lambda *args: None)
    protos = (stage / "recomp/xv_recomp_protos.h").read_text()
    for address in re.findall(r"^void f_([0-9A-F]{8})\(", protos, re.M):
        d.add_root(int(address, 16))
    pc = 0x532E0
    d.add_root(pc)
    d.lift_function(d.functions[pc])
    d.split_blocks(d.functions[pc])
    em = r.Emitter(img, d, hle, img.kernel_imports(), "unused", 1, hooks=hooks)
    em.phase_targets = hooks.phase_targets()
    em.vars = variables
    em.trace_funcs = False
    previous = em.emit_function(d.functions[pc])
    found = []
    for path in (stage / "recomp").glob("code_*.c"):
        for match in re.finditer(r"^void f_000532E0\([^\n]*\)\n\{\n.*?^\}\n",
                                 path.read_text(), re.M | re.S):
            found.append((path, match.group()))
    assert len(found) == 1, "ambiguous or missing retained primary root"
    path, body = found[0]
    phase = f"    XV_PHASE_SCOPE(c, {sorted(hooks.phase_targets()).index(pc)}u);\n"
    header = "void f_000532E0(xctx *restrict c)\n{\n"
    if body.startswith(header + phase):
        previous = previous.replace(phase, "").replace(header, header + phase, 1)
    assert previous.rstrip() == body.rstrip(), "stage/HLE/entry/phase identity mismatch"
    original_hash = hashlib.sha256(body.encode()).hexdigest()
    assert original_hash == "33b84a05a5e6e03d2d2647625ccc0f975eb0db5c4cfe195347d53339dc86a1f9", "unqualified primary body"
    shared = (stage / "recomp/xv_x86rt.h").read_text()
    shared_hash = hashlib.sha256(shared.encode()).hexdigest()
    assert shared_hash == "941cf7000c9fb3ee61d1eea0fd0d451e172d37956666b8d29f97efe5cac7a62b", "unqualified shared header"
    transformed = transform(body, shared)
    assert hashlib.sha256(transformed.encode()).hexdigest() == "d40b1e87c9d135ff5fbd0826bb073c4ed5c1f49a208a6502cbaf3ada09d207cd", "unqualified portal transformation"
    actual = "/* XV_NATIVE_VISIBILITY_PORTAL_LOOP_SCOPE: 000532E0 */\n" + transformed
    assert actual.count("void f_000532E0(") == 1
    assert actual.count("#if " + FLAG + "\n") == 1
    receipt = dict(result="PASS", address="000532E0", unit=path.name,
                   original_sha256=original_hash,
                   new_sha256=hashlib.sha256(actual.encode()).hexdigest(),
                   shared_header_sha256=shared_hash,
                   xbe_sha256=hashlib.sha256(img.data).hexdigest(),
                   symbols_sha256=hashlib.sha256(data).hexdigest(),
                   scope="Primary 532E0 only; existing children, roots and all other bodies retained")
    out.mkdir(parents=True, exist_ok=False)
    (out / "f_000532E0.c").write_text(actual)
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return receipt


if __name__ == "__main__":
    if not __debug__:
        raise SystemExit("Run without Python -O: identity checks require assertions")
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("xbe", "manifest", "symbols", "stage", "output-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(generate(args.xbe, args.manifest, args.symbols,
                              args.stage, args.output_dir), indent=2))
