#!/usr/bin/env python3
"""
ps_pipeline.py - collect Halo's run-time register-combiner programs from an xita.log and turn
the (vertex program, combiner program) pairs the game drew with into Vita fragment shaders.

  log lines used:   [psdef] <hash> <240 hex bytes>        one per unique X_D3DPIXELSHADERDEF
                    [pspair] <vs fnv> <ps hash> ...        one per unique draw pairing
  persistent state: shaders/psdefs/<hash>.bin, shaders/psdefs/pairs.txt   (accumulated over runs)
  Run without logs to regenerate from the accumulated .bin files and pairs.txt.
  Legacy and canonical logged hashes are accepted; captures/pairs retain their identities.
  shaders/halo_pairs.json describes vertex declarations, not logged PS pairings.
  output:           shaders/ps_<canonical_hash>_<vsmask>.frag.cg     (compile with tools/shadercomp -> .gxp)
                    shaders/xv_ps_table.h                   runtime lookup table

Usage: tools/ps_pipeline.py [xita.log ...]
"""
import os, re, sys
from dataclasses import asdict
from pathlib import Path
from psdef_hash import canonicalize, canonical_hash, original_hash

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SH = os.path.join(ROOT, "shaders")
DEFS = os.path.join(SH, "psdefs")
sys.path.insert(0, ROOT)
from dx8_pixelshader_parse import decode_psdef
import pixelshader_recomp_gen as gen
VAR_BITS = [("color0", 1), ("color1", 2), ("texcoord0", 4), ("texcoord1", 8), ("texcoord2", 16), ("texcoord3", 32), ("fog", 64)]
OUT_MAP = {"oD0": "color0", "oD1": "color1", "oT0": "texcoord0", "oT1": "texcoord1", "oT2": "texcoord2", "oT3": "texcoord3", "oFog": "fog"}


def vs_outputs():
    """fnv -> (gxp basename, set of varyings) from the Stage 3 vertex shader sources + xv_layouts.h."""
    lay = open(os.path.join(SH, "xv_layouts.h")).read()
    out = {}
    for m in re.finditer(r'"app0:shaders/(halo_vs_\d+)\.gxp",[^;]*?0x([0-9A-Fa-f]{8})u, \d+u\s*\n};', lay):
        name, fnv = m.group(1), int(m.group(2), 16)
        cg = open(os.path.join(SH, name + ".cg")).read()
        mo = re.search(r"outputs ((?:o\w+ ?)+)", cg)
        vary = {OUT_MAP[o] for o in mo.group(1).split() if o in OUT_MAP} if mo else set()
        out[fnv] = (name, vary)
    return out


def main():
    os.makedirs(DEFS, exist_ok=True)
    pairs_path = os.path.join(DEFS, "pairs.txt")
    pairs = set()
    if os.path.exists(pairs_path):
        pairs = {tuple(l.split()[:2]) for l in open(pairs_path) if l.strip()}
    for log in sys.argv[1:]:
        for line in open(log, "rb").read().decode("latin-1").splitlines():
            m = re.search(r"\[psdef\] ([0-9A-F]{8}) ([0-9A-F]{480})", line)
            if m:
                open(os.path.join(DEFS, m.group(1) + ".bin"), "wb").write(bytes.fromhex(m.group(2)))
            m = re.search(r"\[pspair\] ([0-9A-F]{8}) ([0-9A-F]{8})", line)
            if m and m.group(2) != "00000000":
                pairs.add((m.group(1), m.group(2)))
    open(pairs_path, "w").write("\n".join(" ".join(p) for p in sorted(pairs)) + "\n")

    # Resolve both legacy and new logged identities without overwriting the captures.
    definitions, aliases = {}, {}
    renames = []
    for path in sorted(Path(DEFS).glob("*.bin")):
        raw = path.read_bytes()
        canonical = canonicalize(raw)
        ps = f"{canonical_hash(raw):08X}"
        if int(path.stem, 16) not in (original_hash(raw), int(ps, 16)):
            raise ValueError(f"{path}: neither legacy nor canonical hash matches")
        if ps in definitions and definitions[ps] != canonical:
            raise ValueError(f"canonical FNV collision: {ps}")
        definitions[ps] = canonical
        aliases[path.stem] = ps
        renames.append((path.stem, ps))
    with open(os.path.join(ROOT, "tools/psdef_rename.txt"), "w") as f:
        f.write("# old hash -> canonical hash; preserve the varying-mask/.frag suffix\n")
        for old, new in renames:
            f.write(f"{old} {new}\n")

    vso = vs_outputs()
    table = []
    generated = set()
    paired = set()

    def generate(ps, vary):
        mask = sum(b for v, b in VAR_BITS if v in vary)
        out = f"ps_{ps}_{mask:02X}"
        if out not in generated:
            gen.VARYINGS_AVAILABLE = vary
            cube = os.environ.get("XV_CUBE", "real")
            gen.CUBE_EXPR = {"real": None, "normal": "float4(normalize({tc}.xyz) * 0.5 + 0.5, 1.0)",
                             "const": "float4(0.5, 0.5, 0.5, 1.0)", "black": "float4(0.0, 0.0, 0.0, 1.0)"}[cube]
            cg = gen.generate(asdict(decode_psdef(definitions[ps], 0)), out, False)[0]
            Path(SH, out + ".frag.cg").write_text(cg)
            generated.add(out)
        cg = Path(SH, out + ".frag.cg").read_text()
        cube_mask = sum(1 << int(m) for m in re.findall(r"samplerCUBE tex(\d)", cg))
        return out, cube_mask

    canonical_pairs = set()
    for vs, old in sorted(pairs):
        ps = aliases.get(old, old)
        if ps not in definitions:
            print(f"skip pair {vs}/{old}: def never dumped")
            continue
        canonical_pairs.add((vs, ps))
    for vs, ps in sorted(canonical_pairs):
        fnv = int(vs, 16)
        if fnv not in vso:
            print(f"skip pair {vs}/{ps}: no Stage 3 vertex program for fnv {vs}")
            continue
        name, vary = vso[fnv]
        out, cube_mask = generate(ps, vary)
        paired.add(ps)
        table.append((fnv, int(ps, 16), out, name, cube_mask))
    # Keep a source for every captured structure, even without a known vertex pairing.
    for ps in sorted(definitions.keys() - paired):
        generate(ps, {v for v, _ in VAR_BITS})
    with open(os.path.join(SH, "xv_ps_table.h"), "w") as f:
        f.write("/* generated by tools/ps_pipeline.py: (vertex program fnv, combiner hash) -> fragment program */\n")
        f.write("typedef struct { uint32_t vs_fnv, ps_hash; const char *gxp; uint8_t cube_mask; } xv_ps_entry_t;   /* cube_mask: stages sampled with samplerCUBE */\n")
        f.write("static const xv_ps_entry_t xv_ps_table[] = {\n")
        for fnv, ps, out, name, cube_mask in table:
            f.write(f'    {{ 0x{fnv:08X}u, 0x{ps:08X}u, "app0:shaders/{out}.frag.gxp", 0x{cube_mask:02X} }},   /* {name} */\n')
        f.write("};\n#define XV_PS_TABLE_COUNT (sizeof xv_ps_table / sizeof xv_ps_table[0])\n")
        # what each recompiled vertex program writes (bit 0 color0, 1 color1, 2..5 texcoord0..3, 6 fog):
        # the runtime's fallback fragment choice for pairs not in the table depends on color0
        f.write("typedef struct { uint32_t vs_fnv; uint8_t outputs; } xv_vs_out_t;\n")
        f.write("static const xv_vs_out_t xv_vs_outputs[] = {\n")
        for fnv, (name, vary) in sorted(vso.items()):
            mask = sum(b for v, b in VAR_BITS if v in vary)
            f.write(f"    {{ 0x{fnv:08X}u, 0x{mask:02X} }},   /* {name}: {','.join(sorted(vary))} */\n")
        f.write("};\n#define XV_VS_OUTPUTS_COUNT (sizeof xv_vs_outputs / sizeof xv_vs_outputs[0])\n")
    print(f"{len(table)} pair(s), {len(generated)} fragment shader(s) -> shaders/xv_ps_table.h")


if __name__ == "__main__":
    main()
