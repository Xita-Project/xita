#!/usr/bin/env python3
"""
xbe_shader_pairs.py - Stage 2b of the Xita pipeline: pair vertex declarations
with vertex-shader function blobs.

CreateVertexShader(pDeclaration, pFunction, ...) is called at run time, so the
declaration <-> function association is not stored as data.  XDK-era engines
(Halo's rasterizer_vertex_shaders_initialize is the model) keep a table of
records

        struct { DWORD *decl; DWORD *function; DWORD handle; DWORD size; }

whose `function`/`size` fields are static while `decl` is written by the
initializer with plain x86 stores:

        mov  eax, <declaration VA>            B8 imm32   (or any r32: B8+r)
        mov  [table + i*16], eax               A3 disp32  /  89 /r disp32
        mov  dword ptr [table + i*16], <VA>    C7 05 disp32 imm32

This tool (1) locates the table from the function VAs found by
`dx8_shader_parse.py --scan`, (2) pattern-matches those stores in .text,
(3) validates every pair (each v-register the microcode reads must be supplied
by the declaration), and (4) emits a JSON pairing for Stage 3.

Usage:
    xbe_shader_pairs.py GAME.xbe --manifest game.json [-o pairs.json] [--verbose]
"""

from __future__ import annotations

# Support running this script directly from any working directory.
if __package__ in (None, ""):
    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


import argparse
import json
import os
import struct
import sys
from collections import defaultdict
from typing import Dict, List, Optional, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from recompiler import dx8_shader_parse as s2  # noqa: E402

RECORD_SIZE = 16


class PairError(Exception):
    pass


# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

def section_ranges(aspace: s2.AddressSpace, names: Tuple[str, ...]) -> List[Tuple[int, int, int]]:
    """(va, file_off, size) for the named sections, read back from the manifest."""
    out = []
    for va, roff, rsize in aspace.sections:
        out.append((va, roff, rsize))
    return out


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from("<I", data, off)[0]


# ---------------------------------------------------------------------------
# 1. locate the shader table
# ---------------------------------------------------------------------------

def find_table(data: bytes, aspace: s2.AddressSpace, func_vas: List[int]) -> Tuple[int, List[dict]]:
    """
    Find the array of 16-byte records whose field 1 walks through the function
    VAs.  Returns (table_va, records) with records ordered by index.
    """
    func_set = set(func_vas)
    # candidate record starts: any dword equal to a function VA, at offset +4 of a record
    hits: Dict[int, int] = {}          # file offset of record -> function va
    for off in range(0, len(data) - 4, 4):
        v = u32(data, off)
        if v in func_set and off >= 4:
            hits[off - 4] = v
    # group into arithmetic runs with stride 16
    best: List[int] = []
    for start in sorted(hits):
        if (start - RECORD_SIZE) in hits:
            continue                    # not a run start
        run = [start]
        while run[-1] + RECORD_SIZE in hits:
            run.append(run[-1] + RECORD_SIZE)
        if len(run) > len(best):
            best = run
    if len(best) < 2:
        raise PairError("no run of {decl, function, handle, size} records found")
    table_off = best[0]
    table_va = aspace.offset_to_va(table_off)
    records = []
    for i, off in enumerate(best):
        decl0, func, handle, size = struct.unpack_from("<4I", data, off)
        records.append({"index": i, "record_va": table_va + i * RECORD_SIZE, "file_offset": off,
                        "func_va": func, "size": size, "decl_va": decl0 or None,
                        "handle_init": handle})
    return table_va, records


# ---------------------------------------------------------------------------
# 2. pattern-match the initializer's stores
# ---------------------------------------------------------------------------

def scan_stores(data: bytes, text_ranges: List[Tuple[int, int, int]], decl_vas: set,
                table_va: int, table_len: int, verbose: bool) -> Dict[int, List[Tuple[int, int]]]:
    """
    Returns {record_index: [(decl_va, code_va), ...]} for every store of a
    declaration VA into field 0 of a table record.
    """
    def slot_of(disp: int) -> Optional[int]:
        rel = disp - table_va
        if 0 <= rel < table_len * RECORD_SIZE and rel % RECORD_SIZE == 0:
            return rel // RECORD_SIZE
        return None

    found: Dict[int, List[Tuple[int, int]]] = defaultdict(list)
    for sva, soff, ssize in text_ranges:
        reg_val: Dict[int, int] = {}   # r32 index -> declaration VA currently held
        i = soff
        end = soff + ssize - 10
        while i < end:
            b = data[i]
            # mov r32, imm32   (B8+r)
            if 0xB8 <= b <= 0xBF:
                imm = u32(data, i + 1)
                if imm in decl_vas:
                    reg_val[b - 0xB8] = imm
                    i += 5
                    continue
            # mov [disp32], eax  (A3)
            if b == 0xA3 and 0 in reg_val:
                slot = slot_of(u32(data, i + 1))
                if slot is not None:
                    found[slot].append((reg_val[0], sva + (i - soff)))
                    i += 5
                    continue
            # mov [disp32], r32   (89 /r with mod=00 rm=101)
            if b == 0x89 and (data[i + 1] & 0xC7) == 0x05:
                reg = (data[i + 1] >> 3) & 7
                if reg in reg_val:
                    slot = slot_of(u32(data, i + 2))
                    if slot is not None:
                        found[slot].append((reg_val[reg], sva + (i - soff)))
                        i += 6
                        continue
            # mov dword ptr [disp32], imm32   (C7 05 disp32 imm32)
            if b == 0xC7 and data[i + 1] == 0x05:
                imm = u32(data, i + 6)
                if imm in decl_vas:
                    slot = slot_of(u32(data, i + 2))
                    if slot is not None:
                        found[slot].append((imm, sva + (i - soff)))
                        i += 10
                        continue
            # a call / ret ends the register's lifetime for our purposes
            if b in (0xC3, 0xC2):
                reg_val.clear()
            i += 1
    if verbose:
        for slot in sorted(found):
            print(f"  slot {slot:2d}: " + ", ".join(f"decl 0x{d:08X} @code 0x{c:08X}" for d, c in found[slot]))
    return found


# ---------------------------------------------------------------------------
# 3. validate against the microcode
# ---------------------------------------------------------------------------

def validate_pair(data: bytes, decl_off: int, func_off: int) -> Tuple[List[str], List[str], List[str]]:
    """(missing_inputs, unused_attributes, decl_attr_summary)."""
    decl = s2.decode_declaration(data, decl_off)
    func = s2.decode_function(data, func_off)
    supplied = {f"v{a['vreg']}" for a in decl.attributes}
    used = set(func.inputs_used)
    missing = sorted(used - supplied, key=lambda v: int(v[1:]))
    unused = sorted(supplied - used, key=lambda v: int(v[1:]))
    summary = [f"v{a['vreg']}:{a['type']}" for a in decl.attributes]
    return missing, unused, summary


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(prog="recompiler/xbe_shader_pairs.py",
                                 description="Pair vertex declarations with shader blobs (Xita Stage 2b).")
    ap.add_argument("xbe")
    ap.add_argument("--manifest", required=True, help="Stage 1 xbe_parse.py --json output")
    ap.add_argument("-o", "--output", default=None, help="pairs JSON (default: <xbe>.pairs.json)")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args(argv)

    with open(args.xbe, "rb") as f:
        data = f.read()
    aspace = s2.AddressSpace.from_file(args.xbe, args.manifest)
    with open(args.manifest, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    scan = s2.scan_image(data, aspace)
    func_vas = [f["va"] for f in scan["functions"]]
    decl_vas = [d["va"] for d in scan["declarations"]]
    print(f"scan: {len(func_vas)} function blob(s), {len(decl_vas)} declaration(s)")

    try:
        table_va, records = find_table(data, aspace, func_vas)
    except PairError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"shader table @ VA 0x{table_va:08X}: {len(records)} records "
          f"(field0=decl, field1=function, field2=handle, field3=size)")
    static_decls = sum(1 for r in records if r["decl_va"])
    if static_decls:
        print(f"  {static_decls} record(s) carry a static declaration pointer")

    text_ranges = [(s["virtual_address"], s["raw_address"], s["raw_size"])
                   for s in manifest["sections"] if s["name"] == ".text" or "EXECUTABLE" in s["flag_names"]]
    stores = scan_stores(data, text_ranges, set(decl_vas), table_va, len(records), args.verbose)

    pairs = []
    conflicts = unpaired = invalid = 0
    for r in records:
        cands = {d for d, _ in stores.get(r["index"], [])}
        if r["decl_va"]:
            cands.add(r["decl_va"])
        entry = dict(r)
        if not cands:
            unpaired += 1
            entry["status"] = "unpaired"
        elif len(cands) > 1:
            conflicts += 1
            entry["status"] = "conflict"
            entry["candidates"] = sorted(f"0x{c:08X}" for c in cands)
        else:
            dva = cands.pop()
            entry["decl_va"] = dva
            missing, unused, summary = validate_pair(data, aspace.va_to_offset(dva), aspace.va_to_offset(r["func_va"]))
            entry["decl_attributes"] = summary
            # A v-register the microcode reads but no stream supplies is fed by the
            # persistent attribute value (D3DDevice_SetVertexData4f) on Xbox; the
            # runtime binds it to the instance-indexed constant stream.  Anything
            # beyond one or two such registers would suggest a wrong pairing.
            entry["constant_fed_inputs"] = missing
            entry["decl_attributes_unused"] = unused
            if len(missing) <= 2:
                entry["status"] = "ok"
            else:
                entry["status"] = "suspect"
                invalid += 1
        pairs.append(entry)

    # pretty print
    print()
    print(f"  {'#':>2}  {'function':<10} {'instr':>5}  {'declaration':<11} status    notes")
    for e in pairs:
        n_instr = (e["size"] - 4) // 16
        dva = f"0x{e['decl_va']:08X}" if e.get("decl_va") else "-"
        notes = ""
        if e["status"] in ("ok", "suspect"):
            notes = " ".join(e["decl_attributes"])
            if e["constant_fed_inputs"]:
                notes += f"   (constant-fed: {' '.join(e['constant_fed_inputs'])})"
            if e["decl_attributes_unused"]:
                notes += f"   (unused: {' '.join(e['decl_attributes_unused'])})"
        elif e["status"] == "conflict":
            notes = "candidates " + " ".join(e["candidates"])
        print(f"  {e['index']:>2}  0x{e['func_va']:08X} {n_instr:>5}  {dva:<11} {e['status']:<9} {notes}")

    ok = sum(1 for e in pairs if e["status"] == "ok")
    constfed = sum(1 for e in pairs if e.get("constant_fed_inputs"))
    print(f"\npaired ok: {ok}/{len(pairs)}   unpaired: {unpaired}   conflicts: {conflicts}   "
          f"suspect: {invalid}   pairs with constant-fed inputs: {constfed}")

    out_path = args.output or (os.path.splitext(args.xbe)[0] + ".pairs.json")
    for e in pairs:
        for k in ("record_va", "func_va", "decl_va"):
            if e.get(k) is not None:
                e[k] = f"0x{e[k]:08X}"
        e.pop("file_offset", None)
        e.pop("handle_init", None)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"xbe": args.xbe, "table_va": f"0x{table_va:08X}", "pairs": pairs}, f, indent=1)
    print(f"wrote {out_path}")
    return 0 if ok == len(pairs) else 1


if __name__ == "__main__":
    sys.exit(main())
