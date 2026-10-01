#!/usr/bin/env python3
"""
ps_pipeline.py - collect Halo's run-time register-combiner programs from an xita.log and turn
the (vertex program, combiner program) pairs the game drew with into Vita fragment shaders.

  log lines used:   [psdef] <hash> <240 hex bytes>        one per unique X_D3DPIXELSHADERDEF
                    [pspair] <vs fnv> <ps hash> ...        one per unique draw pairing
  persistent state: shaders/psdefs/<hash>.bin, shaders/psdefs/pairs.txt   (accumulated over runs)
  output:           shaders/ps_<hash>_<vsmask>.frag.cg     (compile with tools/shadercomp -> .gxp)
                    shaders/xv_ps_table.h                   runtime lookup table

Usage: tools/ps_pipeline.py <xita.log> [more logs...]
"""
import json, os, re, sys
from collections import defaultdict
from dataclasses import asdict
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SH = os.path.join(ROOT, "shaders")
DEFS = os.path.join(SH, "psdefs")
PY = sys.executable
sys.path.insert(0, ROOT)
from recompiler.dx8_pixelshader_parse import decode_psdef
from recompiler import pixelshader_recomp_gen as gen
from tools.psdef_hash import original_hash, canonicalize, canonical_hash
from tools.specialize_ps_alpha import specialize
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
    out[0xFFFFFFFE] = ("xv_passthrough", {v for v, _ in VAR_BITS})
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
                path = Path(DEFS, m.group(1) + '.bin')
                data = bytes.fromhex(m.group(2))
                if original_hash(data) != int(m.group(1), 16): raise ValueError(f'invalid capture {m.group(1)}')
                if path.exists():
                    if canonicalize(path.read_bytes()) != canonicalize(data): raise ValueError(f'capture collision {path}')
                else: path.write_bytes(data)
            m = re.search(r"\[pspair\] ([0-9A-F]{8}) ([0-9A-F]{8})", line)
            if m and m.group(2) != "00000000":
                pairs.add((m.group(1), m.group(2)))
    open(pairs_path, "w").write("\n".join(" ".join(p) for p in sorted(pairs)) + "\n")

    vso = vs_outputs()
    groups = defaultdict(list)
    definitions = {}
    captures = {}
    for vs, ps in sorted(pairs):
        fnv = int(vs, 16)
        if fnv not in vso:
            print(f"skip pair {vs}/{ps}: no Stage 3 vertex program for fnv {vs}"); continue
        binp = Path(DEFS) / (ps + ".bin")
        if not binp.exists():
            print(f"skip pair {vs}/{ps}: def {ps} never dumped"); continue
        data = binp.read_bytes()
        if original_hash(data) != int(ps, 16):
            raise ValueError(f"capture hash mismatch: {binp}")
        normalized = canonicalize(data)
        key = canonical_hash(data)
        if key in definitions and definitions[key] != normalized:
            raise ValueError(f"canonical hash collision: {key:08X}")
        definitions[key] = normalized
        captures[int(ps, 16)] = key
        groups[fnv, key].append(ps)

    table = []
    generated = set()
    gen.CUBE_EXPR = os.environ.get('XV_CUBE') or None
    for (fnv, key), members in sorted(groups.items()):
        name, vary = vso[fnv]
        mask = sum(b for v, b in VAR_BITS if v in vary)
        # Retain an existing asset name when possible. All members have the
        # same active program; neither installed filenames nor raw HUD ids move.
        ps = min(members, key=lambda p: (not Path(SH, f"ps_{p}_{mask:02X}.frag.gxp").exists(), p))
        d = asdict(decode_psdef(definitions[key], 0))
        cube_modes = sum(1 << t['index'] for t in d['textures'] if t['mode'] in gen.CUBE_MODES)
        gen.VARYINGS_AVAILABLE = vary
        for c2d in range(16):
            if c2d & ~cube_modes: continue
            out = f"ps_{ps}_{mask:02X}" + (f"_t{c2d:X}" if c2d else "")
            gen.CUBE_2D_MASK = c2d
            cg, warnings, _ = gen.generate(d, out, False)
            if out not in generated:
                generated.add(out)
                path = Path(SH, out + '.frag.cg')
                if not path.exists() or path.read_text() != cg: path.write_text(cg)
                alpha_disabled = Path(SH, out + '_na.frag.cg')
                specialized = specialize(cg)
                if not alpha_disabled.exists() or alpha_disabled.read_text() != specialized:
                    alpha_disabled.write_text(specialized)
                if warnings: print(out, *warnings, sep=': ')
            cube_mask = sum(1 << int(m) for m in re.findall(r"samplerCUBE tex(\d)", cg))
            table.append((fnv, key, c2d, int(ps, 16), out, name, cube_mask, cube_modes))
    gen.CUBE_2D_MASK = 0
    gen.VARYINGS_AVAILABLE = None
    with open(os.path.join(SH, "xv_ps_table.h"), "w") as f:
        f.write("/* generated by tools/ps_pipeline.py; sorted by (vs_fnv, ps_key, c2d_mask). */\n")
        f.write("typedef struct { uint32_t vs_fnv, ps_hash; const char *gxp; uint8_t cube_mask, cube_modes, c2d_mask; uint32_t ps_key; } xv_ps_entry_t;\n")
        f.write("static const xv_ps_entry_t xv_ps_table[] = {\n")
        for fnv, key, c2d, ps, out, name, cube_mask, cube_modes in table:
            f.write(f'    {{ 0x{fnv:08X}u, 0x{ps:08X}u, "app0:shaders/{out}.frag.gxp", 0x{cube_mask:02X}, 0x{cube_modes:02X}, 0x{c2d:02X}, 0x{key:08X}u }}, /* {name} */\n')
        f.write("};\n#define XV_PS_TABLE_COUNT (sizeof xv_ps_table / sizeof xv_ps_table[0])\n")
        f.write("typedef struct { uint32_t hash, key; } xv_ps_capture_key_t;\nstatic const xv_ps_capture_key_t xv_ps_capture_keys[] = {\n")
        for raw, key in sorted(captures.items()): f.write(f"    {{ 0x{raw:08X}u, 0x{key:08X}u }},\n")
        f.write("};\n#define XV_PS_CAPTURE_COUNT (sizeof xv_ps_capture_keys / sizeof xv_ps_capture_keys[0])\n")
        # what each recompiled vertex program writes (bit 0 color0, 1 color1, 2..5 texcoord0..3, 6 fog):
        # the runtime's fallback fragment choice for pairs not in the table depends on color0
        f.write("typedef struct { uint32_t vs_fnv; uint8_t outputs; } xv_vs_out_t;\n")
        f.write("static const xv_vs_out_t xv_vs_outputs[] = {\n")
        for fnv, (name, vary) in sorted(vso.items()):
            mask = sum(b for v, b in VAR_BITS if v in vary)
            f.write(f"    {{ 0x{fnv:08X}u, 0x{mask:02X} }},   /* {name}: {','.join(sorted(vary))} */\n")
        f.write("};\n#define XV_VS_OUTPUTS_COUNT (sizeof xv_vs_outputs / sizeof xv_vs_outputs[0])\n")
    print(f"{len(pairs)} captured pairs -> {len(groups)} canonical pairs, {len(table)} texture variants, {len(generated)} fragment assets")


if __name__ == "__main__":
    main()
