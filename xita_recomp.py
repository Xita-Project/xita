#!/usr/bin/env python3
"""
xita_recomp.py - Stage 4 of the Xita pipeline: x86 -> C static recompiler.

Lifts the game's own code (everything not claimed by an HLE symbol) from an Xbox
XBE to plain C, one function per discovered x86 function, which arm-vita-eabi-gcc
then compiles for the Vita.  Blueprint section 2 + the "emit C, not ARM" decision.

Model
-----
  * Guest memory: XRAM base + (addr & 0x03FFFFFF) via the X_M8/16/32 macros.
  * Registers: xctx.r[8] in x86 encoding order (eax ecx edx ebx esp ebp esi edi);
    esp/ebp point into guest memory (the game's own stacks).
  * Flags: lazy.  Arithmetic stores (kind, op1, op2, res, size); condition codes are
    evaluated on demand by inline helpers; CF is materialised where INC/DEC need it.
  * x87: 8-entry double stack + status word (C0..C3) - MSVC's fnstsw/test/jcc idiom.
  * Control flow: direct calls -> C calls; ret -> C return (the pushed return address
    is popped, never used); indirect calls/jumps -> xv_call() dispatch table over all
    discovered function entries; `jmp [table+reg*4]` -> recovered switch tables.
  * fs:[imm] -> xctx.fs_base (per-fiber KPCR, blueprint section 3.2).
  * HLE: functions named in the symbol JSON are not lifted; calls to them become
    calls to xv_hle_<name>(c).  Kernel thunk slots become xk_<Export>(c).
  * Anything the lifter does not handle emits xv_unimpl(c, eip, "mnemonic") and is
    counted, so coverage is measured, not assumed.

Usage:
    xita_recomp.py GAME.xbe --manifest game.json --symbols halo_symbols.json -o recomp/
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from collections import Counter, defaultdict, deque
from typing import Dict, List, Optional, Set, Tuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from xbox_kernel_exports import KERNEL_EXPORTS, KERNEL_DATA_EXPORTS, KERNEL_ARGC  # noqa: E402

import re as _re
import os as _os
try:
    from iced_x86 import Decoder, Instruction, Mnemonic, OpKind, Register, Code, FlowControl, MemorySize, RflagsBits
except ImportError:
    sys.exit("iced-x86 is required: pip install iced-x86")

MN = {v: k.lower() for k, v in Mnemonic.__dict__.items() if isinstance(v, int)}
REGNAME = {v: k.lower() for k, v in Register.__dict__.items() if isinstance(v, int)}
MSZ = {v: k for k, v in MemorySize.__dict__.items() if isinstance(v, int)}

GUEST_MASK = 0x03FFFFFF

# --------------------------------------------------------------------------------------
#  Image access
# --------------------------------------------------------------------------------------

# dead-flag elimination: the X_FLAGS(...) store emitted by arithmetic lowering (one level of nested parens)
FLAGS_RE = _re.compile(r"X_FLAGS(?:_C)?\((?:[^()]|\([^()]*\))*\);\s?")
# cmp/test/arith + jcc fusion: the flag producer's operands are captured into function-scope locals and
# the branch evaluates the condition inline (no 6-word lazy-flag store, no XF_* reload).
FLAGS_CAP_RE = _re.compile(r"X_FLAGS\((XK_\w+), ((?:[^(),]|\([^()]*\))*), ((?:[^(),]|\([^()]*\))*), ((?:[^(),]|\([^()]*\))*), (\d+)\);\s?")
FUSE_MNEMONICS = {"cmp", "test", "add", "sub", "and", "or", "xor", "inc", "dec", "neg"}
def fused_cond(kind: str, bits: int, cc: str):
    """C expression for condition cc given the captured operands fk_a/fk_b/fk_r of a flag op of `kind`."""
    Z = f"XFI_Z(fk_r,{bits})"; S = f"XFI_S(fk_r,{bits})"
    if kind == "XK_LOGIC": C, O = "0", "0"
    elif kind == "XK_SUB": C, O = f"XFI_C_SUB(fk_a,fk_b,{bits})", f"XFI_O_SUB(fk_a,fk_b,fk_r,{bits})"
    elif kind == "XK_ADD": C, O = f"XFI_C_ADD(fk_a,fk_r,{bits})", f"XFI_O_ADD(fk_a,fk_b,fk_r,{bits})"
    else: return None
    T = {"o": O, "no": f"!{O}", "b": C, "ae": f"!{C}", "e": Z, "ne": f"!{Z}", "be": f"({C}||{Z})", "a": f"(!{C}&&!{Z})",
         "s": S, "ns": f"!{S}", "l": f"({S}!={O})", "ge": f"({S}=={O})", "le": f"({Z}||({S}!={O}))", "g": f"(!{Z}&&({S}=={O}))"}
    return T.get(cc)
# instructions whose lowering relies on the flag record for its own result (rotates/shifts feed CF/OF
# through helpers, adc/sbb read CF, cmpxchg family, string ops with rep) - never strip those
FLAG_KEEP = { Mnemonic.ADC, Mnemonic.SBB, Mnemonic.RCL, Mnemonic.RCR, Mnemonic.CMPXCHG, Mnemonic.CMPXCHG8B,
              Mnemonic.CMPSB, Mnemonic.CMPSW, Mnemonic.CMPSD, Mnemonic.SCASB, Mnemonic.SCASW, Mnemonic.SCASD,
              Mnemonic.SAHF, Mnemonic.LAHF, Mnemonic.POPF, Mnemonic.POPFD, Mnemonic.PUSHF, Mnemonic.PUSHFD }

class Image:
    def __init__(self, xbe_path: str, manifest_path: str):
        self.data = open(xbe_path, "rb").read()
        self.m = json.load(open(manifest_path))
        self.base = self.m["base_address"]
        self.secs = [(s["virtual_address"], s["raw_address"], s["raw_size"], s["virtual_size"], s["name"], s["flag_names"])
                     for s in self.m["sections"]]
        self.entry = self.m["entry_point"]
        self.kernel_thunk = self.m["kernel_thunk"]
        self.tls = self.m["tls_address"]

    def off(self, va: int) -> Optional[int]:
        for sva, roff, rsize, _vs, _n, _f in self.secs:
            if sva <= va < sva + rsize:
                return roff + (va - sva)
        return None

    def section_of(self, va: int):
        for s in self.secs:
            if s[0] <= va < s[0] + max(s[2], s[3]):
                return s
        return None

    DATA_SECTIONS = {".rdata", ".data", ".bss", "DOLBY", "BINKDATA", "$$XTIMAGE", "$$XSIMAGE", "$$XKEYS", "XMV"}

    def is_code(self, va: int) -> bool:
        s = self.section_of(va)
        return bool(s) and "EXECUTABLE" in s[5] and s[4] not in self.DATA_SECTIONS and self.off(va) is not None

    def data_code_pointers(self) -> List[int]:
        """dwords in non-code sections that point into code (vtables, thread entries, callbacks)."""
        out = []
        for sva, roff, rsize, _vs, name, flags in self.secs:
            if self.is_code(sva) or name in ("$$XTIMAGE", "$$XSIMAGE", "$$XKEYS"):
                continue
            for i in range(0, rsize - 3, 4):
                v = struct.unpack_from("<I", self.data, roff + i)[0]
                if self.is_code(v):
                    out.append(v)
        return out

    def u32(self, va: int) -> Optional[int]:
        o = self.off(va)
        return struct.unpack_from("<I", self.data, o)[0] if o is not None and o + 4 <= len(self.data) else None

    def bytes_at(self, va: int, n: int) -> bytes:
        o = self.off(va)
        return self.data[o:o + n] if o is not None else b""

    def kernel_imports(self) -> Dict[int, int]:
        """thunk slot VA -> ordinal"""
        out = {}
        va = self.kernel_thunk
        while True:
            v = self.u32(va)
            if not v or not (v & 0x80000000):
                break
            out[va] = v & 0x7FFFFFFF
            va += 4
        return out


# --------------------------------------------------------------------------------------
#  Discovery
# --------------------------------------------------------------------------------------

class Block:
    def __init__(self, start: int):
        self.start = start
        self.insns: List[Instruction] = []
        self.end = start
        self.succ: List[int] = []


class Function:
    def __init__(self, entry: int):
        self.entry = entry
        self.blocks: Dict[int, Block] = {}
        self.switch_tables: Dict[int, List[int]] = {}      # insn ip -> targets
        self.calls: Set[int] = set()


class Discovery:
    def __init__(self, img: Image, hle_funcs: Dict[int, dict], kthunks: Dict[int, int], log):
        self.img = img
        self.hle = hle_funcs
        self.kthunks = kthunks
        self.log = log
        self.functions: Dict[int, Function] = {}
        self.queue: deque = deque()
        self.stats = Counter()
        self.candidates: Set[int] = set()          # code addresses seen as immediates (function pointers)

    def decode_at(self, va: int) -> Optional[Instruction]:
        b = self.img.bytes_at(va, 16)
        if not b:
            return None
        d = Decoder(32, b, ip=va)
        ins = d.decode()
        return None if ins.is_invalid else ins

    def add_root(self, va: int):
        if va in self.hle or va in self.functions or not self.img.is_code(va):
            return
        self.functions[va] = Function(va)
        self.queue.append(va)

    def plausible_entry(self, va: int) -> bool:
        """Cheap validation for pointer-derived roots: must decode 3 instructions without hitting garbage."""
        ip = va
        JUNK = {"int3", "hlt", "in", "out", "into", "iretd", "iret", "lds", "les", "lss", "lfs", "lgs", "bound", "arpl", "aaa", "aas",
                "aam", "aad", "daa", "das", "salc", "insb", "insd", "outsb", "outsd", "retf", "xlatb", "cli", "sti", "wbinvd", "invd",
                "lock", "enter", "fs", "gs", "int", "icebp", "ud2", "cmc", "stc", "lar", "lsl", "sgdt", "lgdt", "lidt"}
        for _ in range(6):
            ins = self.decode_at(ip)
            if ins is None:
                return False
            mn = MN[ins.mnemonic]
            if mn in JUNK or ins.has_lock_prefix:
                return False
            for oi in range(ins.op_count):
                if ins.op_kind(oi) == OpKind.REGISTER and REGNAME[ins.op_register(oi)] in ("cs", "ds", "es", "ss", "fs", "gs"):
                    return False
            if ins.segment_prefix != Register.NONE and REGNAME[ins.segment_prefix] not in ("fs", "ds", "cs", "ss", "es"):
                return False
            b = self.img.bytes_at(ip, 2)
            if b == b"\x00\x00":
                return False
            if ins.flow_control in (FlowControl.RETURN, FlowControl.UNCONDITIONAL_BRANCH, FlowControl.INDIRECT_BRANCH):
                return True
            ip = ins.next_ip
        return True

    def in_lifted_block(self, va: int) -> bool:
        for fn in self.functions.values():
            for b in fn.blocks.values():
                if b.start < va < b.end:
                    return True
        return False

    def run(self):
        rounds = 0
        while True:
            while self.queue:
                self.lift_function(self.functions[self.queue.popleft()])
            new = [va for va in self.candidates if va not in self.functions and va not in self.hle and self.plausible_entry(va)]
            self.candidates.clear()
            if not new:
                break
            # roots inside already-lifted blocks are allowed: a call to a noreturn function makes the lifter
            # fall through into the next function, which vtables then reference directly
            for va in new:
                self.add_root(va)
            self.stats["pointer_roots"] += len(new)
            rounds += 1

    def lift_function(self, fn: Function):
        work = [fn.entry]
        seen: Set[int] = set()
        while work:
            start = work.pop()
            if start in seen or start in fn.blocks:
                continue
            seen.add(start)
            blk = Block(start)
            ip = start
            while True:
                if ip in fn.blocks and ip != start:            # ran into an existing block
                    blk.succ.append(ip)
                    break
                ins = self.decode_at(ip)
                if ins is None:
                    self.stats["decode_fail"] += 1
                    break
                blk.insns.append(ins)
                ip = ins.next_ip
                fc = ins.flow_control
                mn = MN[ins.mnemonic]
                for oi in range(ins.op_count):
                    k = ins.op_kind(oi)
                    if k == OpKind.IMMEDIATE32 or k == OpKind.IMMEDIATE32TO64:
                        imm = ins.immediate(oi) & 0xFFFFFFFF
                        if self.img.is_code(imm):
                            self.candidates.add(imm)
                if fc == FlowControl.NEXT or fc == FlowControl.CALL or fc == FlowControl.INDIRECT_CALL:
                    if fc == FlowControl.CALL:
                        tgt = ins.near_branch_target
                        fn.calls.add(tgt)
                        if tgt in self.kthunks or tgt in self.hle:
                            pass
                        else:
                            self.add_root(tgt)
                    elif fc == FlowControl.INDIRECT_CALL:
                        self.stats["indirect_call"] += 1
                    # a call to a HLE symbol that never returns? (XapiInitProcess etc.) - assume returns
                    if mn == "int3":
                        break
                    if ip in fn.blocks:
                        blk.succ.append(ip)
                        break
                    continue
                if fc == FlowControl.UNCONDITIONAL_BRANCH:
                    tgt = ins.near_branch_target
                    if tgt in self.functions or tgt in self.hle or tgt in self.kthunks:
                        pass                                   # tail call, emitted as call+return
                    elif self.img.is_code(tgt):
                        blk.succ.append(tgt)
                        work.append(tgt)
                    break
                if fc == FlowControl.CONDITIONAL_BRANCH:
                    tgt = ins.near_branch_target
                    if self.img.is_code(tgt):
                        blk.succ.append(tgt)
                        work.append(tgt)
                    blk.succ.append(ip)
                    work.append(ip)
                    break
                if fc == FlowControl.INDIRECT_BRANCH:
                    targets = self.switch_targets(fn, ins)
                    if targets:
                        fn.switch_tables[ins.ip] = targets
                        for _, t in targets:
                            blk.succ.append(t)
                            work.append(t)
                    else:
                        self.stats["indirect_jmp_dispatch"] += 1
                    break
                if fc == FlowControl.RETURN:
                    break
                if fc in (FlowControl.INTERRUPT, FlowControl.EXCEPTION):
                    break
                # XBEGIN etc: treat as end
                break
            blk.end = ip
            fn.blocks[start] = blk
        # split blocks that others jump into the middle of
        self.split_blocks(fn)

    def split_blocks(self, fn: Function):
        starts = sorted(fn.blocks)
        targets = set()
        for b in fn.blocks.values():
            targets.update(b.succ)
        for s in starts:
            b = fn.blocks[s]
            for i, ins in enumerate(b.insns):
                if i and ins.ip in targets and ins.ip not in fn.blocks:
                    nb = Block(ins.ip)
                    nb.insns = b.insns[i:]
                    nb.end = b.end
                    nb.succ = b.succ
                    b.insns = b.insns[:i]
                    b.end = ins.ip
                    b.succ = [ins.ip]
                    fn.blocks[ins.ip] = nb
                    break

    def switch_targets(self, fn: Function, ins: Instruction) -> List[int]:
        """jmp dword ptr [disp32 + reg*4] -> table entries that are code addresses."""
        if ins.op0_kind != OpKind.MEMORY or ins.memory_index == Register.NONE or ins.memory_index_scale != 4:
            return []
        if ins.memory_base != Register.NONE:
            return []
        table = (ins.memory_displacement & 0xFFFFFFFF)
        out = []
        for i in range(512):
            v = self.img.u32(table + i * 4)
            if v is None or not self.img.is_code(v):
                break
            out.append((i, v))
        # Tables indexed from the top down: the CRT's memcpy/memmove tails do `neg ecx; jmp [ecx*4+T]`
        # with ecx <= 0, so the live entries sit BELOW the displacement.  Walk downward as well and keep
        # the (possibly negative) index with each target; the emitter switches on the 32-bit register.
        for i in range(1, 64):
            v = self.img.u32(table - i * 4)
            if v is None or not self.img.is_code(v):
                break
            out.append((-i, v))
        return out


# --------------------------------------------------------------------------------------
#  C emission
# --------------------------------------------------------------------------------------

REG32 = {"eax": 0, "ecx": 1, "edx": 2, "ebx": 3, "esp": 4, "ebp": 5, "esi": 6, "edi": 7}
REG16 = {"ax": 0, "cx": 1, "dx": 2, "bx": 3, "sp": 4, "bp": 5, "si": 6, "di": 7}
REG8L = {"al": 0, "cl": 1, "dl": 2, "bl": 3}
REG8H = {"ah": 0, "ch": 1, "dh": 2, "bh": 3}
COND = {"o": "XF_O(c)", "no": "!XF_O(c)", "b": "XF_C(c)", "ae": "!XF_C(c)", "e": "XF_Z(c)", "ne": "!XF_Z(c)",
        "be": "(XF_C(c)||XF_Z(c))", "a": "(!XF_C(c)&&!XF_Z(c))", "s": "XF_S(c)", "ns": "!XF_S(c)", "p": "XF_P(c)",
        "np": "!XF_P(c)", "l": "(XF_S(c)!=XF_O(c))", "ge": "(XF_S(c)==XF_O(c))", "le": "(XF_Z(c)||(XF_S(c)!=XF_O(c)))",
        "g": "(!XF_Z(c)&&(XF_S(c)==XF_O(c)))"}
JCC_ALIAS = {"jz": "je", "jnz": "jne", "jc": "jb", "jnc": "jae", "jnae": "jb", "jnb": "jae", "jna": "jbe", "jnbe": "ja",
             "jnge": "jl", "jnl": "jge", "jng": "jle", "jnle": "jg", "jpe": "jp", "jpo": "jnp"}

X87_SIZES = {"float32": "f32", "float64": "f64", "float80": "f80", "int16": "i16", "int32": "i32", "int64": "i64"}


class Emitter:
    def __init__(self, img: Image, disc: Discovery, hle: Dict[int, dict], kthunks: Dict[int, int], outdir: str, nfiles: int):
        self.img, self.disc, self.hle, self.kthunks = img, disc, hle, kthunks
        self.outdir = outdir
        self.nfiles = nfiles
        self.game_main: Optional[int] = None
        self.vars: Dict[str, int] = {}
        self.trace = False
        self.trace_funcs = False
        self.cur_fn = 0
        self.unimpl = Counter()
        self.stats = Counter()
        self.hle_used: Set[str] = set()
        self.kernel_used: Set[str] = set()

    # ---- operand helpers -----------------------------------------------------------
    def reg(self, r: int, size: int) -> str:
        n = REGNAME[r]
        if n in REG32:
            return f"c->r[{REG32[n]}]"
        if n in REG16:
            return f"X_R16({REG16[n]})"
        if n in REG8L:
            return f"X_R8L({REG8L[n]})"
        if n in REG8H:
            return f"X_R8H({REG8H[n]})"
        if n.startswith("xmm"):
            return f"c->xmm[{n[3:]}]"
        if n.startswith("mm"):
            return f"c->mm[{n[2:]}]"
        if n in ("cs", "ds", "es", "fs", "gs", "ss"):
            return "c->scratch"
        if n.startswith("st"):
            return f"X_ST({n[2:].strip('()') or 0})"
        return f"/*reg {n}*/0"

    def reg_lvalue(self, r: int) -> str:
        return self.reg(r, 0)

    def addr(self, ins: Instruction) -> str:
        parts = []
        if ins.memory_base != Register.NONE:
            parts.append(self.reg(ins.memory_base, 4) if REGNAME[ins.memory_base] in REG32 else f"({self.reg(ins.memory_base,2)})")
        if ins.memory_index != Register.NONE:
            idx = self.reg(ins.memory_index, 4)
            parts.append(f"({idx}*{ins.memory_index_scale})" if ins.memory_index_scale != 1 else idx)
        disp = (ins.memory_displacement & 0xFFFFFFFF) if (ins.memory_displacement & 0xFFFFFFFF) < 0x80000000 or not parts else (ins.memory_displacement & 0xFFFFFFFF)
        if disp or not parts:
            parts.append(f"0x{disp:X}u")
        expr = "+".join(parts)
        seg = REGNAME.get(ins.segment_prefix, "")
        if seg == "fs":
            expr = f"c->fs_base+({expr})"
        return f"({expr})"

    def mem(self, ins: Instruction, size: int) -> str:
        return f"X_M{size*8}({self.addr(ins)})"

    def op_size(self, ins: Instruction, i: int) -> int:
        k = ins.op_kind(i)
        if k == OpKind.REGISTER:
            n = REGNAME[ins.op_register(i)]
            if n in REG32 or n.startswith("xmm"): return 4
            if n.startswith("mm"): return 8
            if n in REG16: return 2
            return 1
        if k == OpKind.MEMORY:
            sz = ins.memory_size
            name = MSZ.get(sz, "").upper()
            if name.startswith("PACKED64") or name in ("UINT64", "INT64", "FLOAT64", "QWORD"): return 8
            if name.startswith("PACKED128"): return 16
            return {"UINT8": 1, "INT8": 1, "UINT16": 2, "INT16": 2, "UINT32": 4, "INT32": 4, "FLOAT32": 4}.get(name, 4)
        if k in (OpKind.IMMEDIATE8, OpKind.IMMEDIATE8_2ND): return 1
        if k == OpKind.IMMEDIATE16: return 2
        return 4

    def operand(self, ins: Instruction, i: int, size: int) -> str:
        k = ins.op_kind(i)
        if k == OpKind.REGISTER:
            return self.reg(ins.op_register(i), size)
        if k == OpKind.MEMORY:
            return self.mem(ins, size)
        if k in (OpKind.IMMEDIATE8, OpKind.IMMEDIATE16, OpKind.IMMEDIATE32, OpKind.IMMEDIATE8TO16,
                 OpKind.IMMEDIATE8TO32, OpKind.IMMEDIATE32TO64, OpKind.IMMEDIATE8TO64, OpKind.IMMEDIATE64):
            v = ins.immediate(i) & (0xFFFFFFFF if size >= 4 else (1 << (8 * size)) - 1)
            return f"0x{v:X}u"
        if k in (OpKind.NEAR_BRANCH32, OpKind.NEAR_BRANCH16):
            return f"0x{ins.near_branch_target:X}u"
        return "/*?op*/0"

    # ---- flags ---------------------------------------------------------------------
    def setflags(self, kind: str, op1: str, op2: str, res: str, size: int) -> str:
        return f"X_FLAGS({kind}, {op1}, {op2}, {res}, {size*8});"

    # ---- instruction lowering ------------------------------------------------------
    def lower(self, fn: Function, ins: Instruction, out: List[str]):
        mn = MN[ins.mnemonic]
        rep = ins.has_rep_prefix or ins.has_repe_prefix or ins.has_repne_prefix
        ip = ins.ip
        nxt = ins.next_ip
        out.append(f"    /* {ip:08X}  {str(ins)} */")

        def U():
            self.unimpl[mn] += 1
            out.append(f'    xv_unimpl(c, 0x{ip:X}u, "{mn}");')

        # ---- data movement --------------------------------------------------------
        if mn == "mov":
            sz = self.op_size(ins, 0)
            if ins.op0_kind == OpKind.REGISTER and REGNAME[ins.op0_register] in ("fs", "es", "ds", "ss", "gs", "cs"):
                return
            if ins.op1_kind == OpKind.REGISTER and REGNAME[ins.op1_register] in ("fs", "es", "ds", "ss", "gs", "cs"):
                out.append(f"    {self.operand(ins,0,sz)} = 0;"); return
            out.append(f"    {self.operand(ins,0,sz)} = {self.operand(ins,1,sz)};"); return
        if mn in ("movzx", "movsx"):
            dsz, ssz = self.op_size(ins, 0), self.op_size(ins, 1)
            src = self.operand(ins, 1, ssz)
            cast = f"(int{ssz*8}_t)" if mn == "movsx" else f"(uint{ssz*8}_t)"
            out.append(f"    {self.operand(ins,0,dsz)} = (uint{dsz*8}_t)({cast}{src});"); return
        if mn == "lea":
            out.append(f"    {self.operand(ins,0,4)} = {self.addr(ins)};"); return
        if mn == "xchg":
            sz = self.op_size(ins, 0)
            out.append(f"    {{ uint{sz*8}_t t = {self.operand(ins,0,sz)}; {self.operand(ins,0,sz)} = {self.operand(ins,1,sz)}; {self.operand(ins,1,sz)} = t; }}"); return
        if mn == "push":
            sz = self.op_size(ins, 0) if ins.op0_kind != OpKind.IMMEDIATE8TO32 else 4
            out.append(f"    X_PUSH32({self.operand(ins,0,4)});"); return
        if mn == "pop":
            out.append(f"    {self.operand(ins,0,4)} = X_POP32();"); return
        if mn == "pushfd":
            out.append("    X_PUSH32(xf_eflags(c));"); return
        if mn == "popfd":
            out.append("    xf_set_eflags(c, X_POP32());"); return
        if mn == "leave":
            out.append("    c->r[4] = c->r[5]; c->r[5] = X_POP32();"); return
        if mn in ("cdq",):
            out.append("    c->r[2] = ((int32_t)c->r[0] < 0) ? 0xFFFFFFFFu : 0u;"); return
        if mn == "cwde":
            out.append("    c->r[0] = (uint32_t)(int32_t)(int16_t)c->r[0];"); return
        if mn == "cbw":
            out.append("    X_R16(0) = (uint16_t)(int16_t)(int8_t)c->r[0];"); return
        if mn == "cwd":
            out.append("    X_R16(2) = ((int16_t)c->r[0] < 0) ? 0xFFFFu : 0u;"); return
        if mn in ("nop", "fwait", "wait", "pause", "prefetcht1", "prefetcht0", "prefetchnta", "sfence", "lfence", "mfence", "femms", "emms"):
            return
        if mn in ("cld", "std"):
            out.append(f"    c->df = {1 if mn == 'std' else 0};"); return
        if mn == "xlatb":
            out.append("    X_R8L(0) = X_M8(c->r[3] + X_R8L(0));"); return
        if mn == "pushad":
            out.append("    { uint32_t sp_ = c->r[4]; X_PUSH32(c->r[0]); X_PUSH32(c->r[1]); X_PUSH32(c->r[2]); X_PUSH32(c->r[3]); X_PUSH32(sp_); X_PUSH32(c->r[5]); X_PUSH32(c->r[6]); X_PUSH32(c->r[7]); }"); return
        if mn == "popad":
            out.append("    c->r[7] = X_POP32(); c->r[6] = X_POP32(); c->r[5] = X_POP32(); (void)X_POP32(); c->r[3] = X_POP32(); c->r[2] = X_POP32(); c->r[1] = X_POP32(); c->r[0] = X_POP32();"); return
        if mn == "enter":
            out.append(f"    X_PUSH32(c->r[5]); c->r[5] = c->r[4]; c->r[4] -= {ins.immediate(0) & 0xFFFF}u;"); return
        if mn in ("clc", "stc", "cmc"):
            out.append(f"    c->f_cf = {'0' if mn == 'clc' else '1' if mn == 'stc' else '!XF_C(c)'}; c->f_cf_override = 1;"); return
        if mn == "rdtsc":
            out.append("    { uint64_t t_ = x_rdtsc(); c->r[0] = (uint32_t)t_; c->r[2] = (uint32_t)(t_ >> 32); }"); return
        if mn in ("cli", "sti", "wbinvd", "invd", "cpuid"):
            out.append(f"    /* {mn}: privileged, ignored */" if mn != "cpuid" else "    c->r[0] = 0x673; c->r[3] = c->r[1] = 0; c->r[2] = 0x0383F9FF;"); return
        if mn == "int3":
            out.append(f"    xv_trap(c, 0x{ip:X}u);"); return
        if mn == "hlt":
            out.append(f"    xv_trap(c, 0x{ip:X}u);"); return

        # ---- arithmetic / logic ---------------------------------------------------
        ARITH = {"add": "+", "sub": "-", "and": "&", "or": "|", "xor": "^", "adc": "+", "sbb": "-"}
        if mn in ARITH or mn in ("cmp", "test"):
            sz = self.op_size(ins, 0)
            a = self.operand(ins, 0, sz); b = self.operand(ins, 1, sz)
            T = f"uint{sz*8}_t"
            if mn == "cmp":
                out.append(f"    {{ {T} a_ = {a}, b_ = {b}; {self.setflags('XK_SUB','a_','b_',f'({T})(a_-b_)',sz)} }}"); return
            if mn == "test":
                out.append(f"    {{ {T} r_ = {a} & {b}; {self.setflags('XK_LOGIC','0','0','r_',sz)} }}"); return
            if mn in ("adc", "sbb"):
                cf = "XF_C(c)"
                out.append(f"    {{ {T} a_ = {a}, b_ = {b}; uint32_t cf_ = {cf}; {T} r_ = ({T})(a_ {ARITH[mn]} b_ {ARITH[mn]} cf_); "
                           f"X_FLAGS_C({'XK_ADC' if mn=='adc' else 'XK_SBB'}, a_, b_, r_, {sz*8}, cf_); {a} = r_; }}"); return
            kind = {"add": "XK_ADD", "sub": "XK_SUB"}.get(mn, "XK_LOGIC")
            out.append(f"    {{ {T} a_ = {a}, b_ = {b}; {T} r_ = ({T})(a_ {ARITH[mn]} b_); {self.setflags(kind,'a_','b_','r_',sz)} {a} = r_; }}"); return
        if mn in ("inc", "dec"):
            sz = self.op_size(ins, 0); a = self.operand(ins, 0, sz); T = f"uint{sz*8}_t"
            out.append(f"    {{ uint32_t cf_ = XF_C(c); {T} a_ = {a}; {T} r_ = ({T})(a_ {'+' if mn=='inc' else '-'} 1); "
                       f"{self.setflags('XK_ADD' if mn=='inc' else 'XK_SUB','a_','1','r_',sz)} c->f_cf_override = 1; c->f_cf = cf_; {a} = r_; }}"); return
        if mn == "neg":
            sz = self.op_size(ins, 0); a = self.operand(ins, 0, sz); T = f"uint{sz*8}_t"
            out.append(f"    {{ {T} a_ = {a}; {T} r_ = ({T})(0 - a_); {self.setflags('XK_SUB','0','a_','r_',sz)} {a} = r_; }}"); return
        if mn == "not":
            sz = self.op_size(ins, 0); a = self.operand(ins, 0, sz)
            out.append(f"    {a} = (uint{sz*8}_t)~{a};"); return
        if mn in ("shl", "sal", "shr", "sar", "rol", "ror", "rcl", "rcr"):
            sz = self.op_size(ins, 0); a = self.operand(ins, 0, sz)
            cnt = self.operand(ins, 1, 1) if ins.op_count > 1 else "1u"
            out.append(f"    {a} = x_{mn if mn!='sal' else 'shl'}{sz*8}(c, {a}, {cnt});"); return
        if mn in ("shld", "shrd"):
            a = self.operand(ins, 0, 4); b = self.operand(ins, 1, 4); cnt = self.operand(ins, 2, 1)
            out.append(f"    {a} = x_{mn}(c, {a}, {b}, {cnt});"); return
        if mn == "imul":
            if ins.op_count == 1:
                sz = self.op_size(ins, 0)
                out.append(f"    x_imul1_{sz*8}(c, {self.operand(ins,0,sz)});"); return
            sz = self.op_size(ins, 0); dst = self.operand(ins, 0, sz)
            src1 = self.operand(ins, 1, sz) if ins.op_count >= 2 else dst
            src2 = self.operand(ins, 2, sz) if ins.op_count == 3 else src1
            if ins.op_count == 2:
                src2 = src1; src1 = dst
            out.append(f"    {dst} = x_imul{sz*8}(c, {src1}, {src2});"); return
        if mn == "mul":
            sz = self.op_size(ins, 0); out.append(f"    x_mul_{sz*8}(c, {self.operand(ins,0,sz)});"); return
        if mn in ("div", "idiv"):
            sz = self.op_size(ins, 0); out.append(f"    x_{mn}_{sz*8}(c, {self.operand(ins,0,sz)}, 0x{ip:X}u);"); return
        if mn.startswith("set"):
            cc = JCC_ALIAS.get("j" + mn[3:], "j" + mn[3:])[1:]
            if cc not in COND:
                U(); return
            out.append(f"    {self.operand(ins,0,1)} = {COND[cc]} ? 1 : 0;"); return
        if mn == "sahf":
            out.append("    xf_set_eflags(c, (xf_eflags(c) & ~0xFFu) | X_R8H(0));"); return
        if mn == "lahf":
            out.append("    X_R8H(0) = (uint8_t)(xf_eflags(c) & 0xD5u) | 0x02u;"); return
        if mn.startswith("cmov"):
            cc = JCC_ALIAS.get("j" + mn[4:], "j" + mn[4:])[1:]
            sz = self.op_size(ins, 0)
            out.append(f"    if ({COND[cc]}) {self.operand(ins,0,sz)} = {self.operand(ins,1,sz)};"); return
        if mn in ("bt", "bts", "btr", "btc"):
            sz = self.op_size(ins, 0); a = self.operand(ins, 0, sz); b = self.operand(ins, 1, sz if ins.op1_kind != OpKind.IMMEDIATE8 else 1)
            out.append(f"    {{ uint32_t bit_ = ({b}) & {sz*8-1}; c->f_cf_override = 1; c->f_cf = ({a} >> bit_) & 1; "
                       + ({"bt": "", "bts": f"{a} |= (1u << bit_);", "btr": f"{a} &= ~(1u << bit_);", "btc": f"{a} ^= (1u << bit_);"}[mn]) + " }"); return
        if mn in ("bsf", "bsr"):
            a = self.operand(ins, 0, 4); b = self.operand(ins, 1, 4)
            out.append(f"    {{ uint32_t v_ = {b}; X_FLAGS(XK_LOGIC, 0, 0, v_, 32); if (v_) {a} = {'__builtin_ctz(v_)' if mn=='bsf' else '31 - __builtin_clz(v_)'}; }}"); return
        if mn == "xadd":
            sz = self.op_size(ins, 0); a = self.operand(ins, 0, sz); b = self.operand(ins, 1, sz); T = f"uint{sz*8}_t"
            out.append(f"    {{ {T} a_ = {a}, b_ = {b}; {T} r_ = ({T})(a_ + b_); {self.setflags('XK_ADD','a_','b_','r_',sz)} {b} = a_; {a} = r_; }}"); return
        if mn == "cmpxchg":
            sz = self.op_size(ins, 0); a = self.operand(ins, 0, sz); b = self.operand(ins, 1, sz); T = f"uint{sz*8}_t"
            acc = {1: "X_R8L(0)", 2: "X_R16(0)", 4: "c->r[0]"}[sz]
            out.append(f"    {{ {T} a_ = {a}, acc_ = {acc}; {self.setflags('XK_SUB','acc_','a_',f'({T})(acc_-a_)',sz)} if (acc_ == a_) {a} = {b}; else {acc} = a_; }}"); return

        # ---- string ops -----------------------------------------------------------
        if mn in ("movsb", "movsw", "movsd", "stosb", "stosw", "stosd", "lodsb", "lodsw", "lodsd", "cmpsb", "cmpsw", "cmpsd", "scasb", "scasw", "scasd"):
            sz = {"b": 1, "w": 2, "d": 4}[mn[-1]]
            kind = "rep" if ins.has_rep_prefix else ("repe" if ins.has_repe_prefix else ("repne" if ins.has_repne_prefix else "once"))
            # iced reports rep for movs/stos/lods as has_rep_prefix; repe for cmps/scas
            if mn[:4] in ("cmps", "scas") and ins.has_rep_prefix:
                kind = "repe"
            out.append(f"    x_str_{mn[:4]}(c, {sz}, X_STR_{kind.upper()});"); return

        # ---- control flow -----------------------------------------------------------
        if mn == "call":
            if ins.op0_kind == OpKind.NEAR_BRANCH32:
                tgt = ins.near_branch_target
                out.append(f"    X_PUSH32(0x{nxt:X}u);")
                if self.trace and (tgt in self.hle or tgt in self.kthunks):
                    out.append(f'    if (xv_trace_enabled) xv_trace_call(c, "{self.trace_name(tgt)}", {self.trace_argc(tgt)});')
                out.append(f"    {self.call_expr(tgt)};")
                if self.trace_funcs:
                    out.append(f"    XV_FN_BACK(0x{self.cur_fn:08X}u);")      # sampling profiler: time after the call is ours again
                return
            # indirect: through kernel thunk slot?  call [slot]
            if ins.op0_kind == OpKind.MEMORY and ins.memory_base == Register.NONE and ins.memory_index == Register.NONE:
                slot = (ins.memory_displacement & 0xFFFFFFFF)
                if slot in self.kthunks:
                    name = KERNEL_EXPORTS.get(self.kthunks[slot], f"ordinal_{self.kthunks[slot]}")
                    self.kernel_used.add(name)
                    out.append(f"    X_PUSH32(0x{nxt:X}u);")
                    if self.trace:
                        out.append(f'    if (xv_trace_enabled) xv_trace_call(c, "{name}", {KERNEL_ARGC.get(name, 0)});')
                    out.append(f"    XV_HLE_CALL(0x{slot:X}u, xk_{name});")
                    return
            out.append(f"    {{ uint32_t t_ = {self.operand(ins,0,4)}; X_PUSH32(0x{nxt:X}u); xv_call(c, t_); }}")
            return
        if mn == "ret":
            n = ins.immediate(0) if ins.op_count else 0
            out.append(f"    c->r[4] += {4 + n}; return;")
            return
        if mn == "jmp":
            if ins.op0_kind == OpKind.NEAR_BRANCH32:
                tgt = ins.near_branch_target
                if tgt in fn.blocks:
                    if tgt <= ip:
                        out.append("    X_PREEMPT();")
                    out.append(f"    goto L_{tgt:08X};")
                else:
                    out.append(f"    {self.call_expr(tgt)}; return;")             # tail call
                return
            if ins.ip in fn.switch_tables:
                idx = self.reg(ins.memory_index, 4)
                out.append(f"    switch ({idx}) {{")
                for i, t in fn.switch_tables[ins.ip]:
                    out.append(f"    case 0x{i & 0xFFFFFFFF:X}u: goto L_{t:08X};")
                out.append(f"    default: xv_trap(c, 0x{ip:X}u); return; }}")
                return
            out.append(f"    xv_call(c, {self.operand(ins,0,4)}); return;")       # indirect tail jump
            return
        if mn.startswith("j") and mn not in ("jmp",):
            cc = JCC_ALIAS.get(mn, mn)[1:]
            tgt = ins.near_branch_target
            if mn in ("jecxz", "jcxz"):
                cond = "c->r[1] == 0" if mn == "jecxz" else "X_R16(1) == 0"
            elif getattr(self, "fused_cond", None):
                cond = self.fused_cond; self.fused_cond = None
            elif cc in COND:
                cond = COND[cc]
            else:
                U(); return
            if tgt in fn.blocks:
                if tgt <= ip:
                    out.append(f"    if ({cond}) {{ X_PREEMPT(); goto L_{tgt:08X}; }}")
                else:
                    out.append(f"    if ({cond}) goto L_{tgt:08X};")
            else:
                out.append(f"    if ({cond}) {{ {self.call_expr(tgt)}; return; }}")
            return
        if mn in ("loop", "loope", "loopne"):
            tgt = ins.near_branch_target
            extra = {"loop": "", "loope": " && XF_Z(c)", "loopne": " && !XF_Z(c)"}[mn]
            out.append(f"    if (--c->r[1] != 0{extra}) {{ X_PREEMPT(); goto L_{tgt:08X}; }}"); return

        # ---- x87 --------------------------------------------------------------------
        if mn.startswith("f") and mn not in ("fs",):
            self.lower_x87(ins, mn, out, U); return

        # ---- MMX (Bink) -----------------------------------------------------------------
        MMX_BIN = {"paddb", "paddw", "paddd", "psubb", "psubw", "psubd", "paddsb", "paddsw", "psubsb", "psubsw", "paddusb", "paddusw",
                   "psubusb", "psubusw", "pmullw", "pmulhw", "pmulhuw", "pcmpeqb", "pcmpeqw", "pcmpeqd", "pcmpgtb", "pcmpgtw", "pcmpgtd",
                   "pavgb", "pavgw", "pminub", "pmaxub", "pminsw", "pmaxsw", "pand", "pandn", "por", "pxor", "pmaddwd",
                   "punpcklbw", "punpcklwd", "punpckldq", "punpckhbw", "punpckhwd", "punpckhdq", "packsswb", "packuswb", "packssdw"}
        MMX_SHIFT = {"psllw", "pslld", "psllq", "psrlw", "psrld", "psrlq", "psraw", "psrad"}
        is_mm0 = ins.op_count and ins.op0_kind == OpKind.REGISTER and REGNAME[ins.op0_register].startswith("mm")
        is_mm1 = ins.op_count > 1 and ins.op1_kind == OpKind.REGISTER and REGNAME[ins.op1_register].startswith("mm")
        if mn == "emms":
            return
        if mn == "movq" and (is_mm0 or is_mm1):
            out.append(f"    {self.operand(ins,0,8)} = {self.operand(ins,1,8)};"); return
        if mn == "movd" and (is_mm0 or is_mm1):
            if is_mm0:
                out.append(f"    {self.operand(ins,0,8)} = (uint64_t){self.operand(ins,1,4)};")
            else:
                out.append(f"    {self.operand(ins,0,4)} = (uint32_t){self.operand(ins,1,8)};")
            return
        if mn in MMX_BIN and is_mm0:
            if mn == "pxor" and is_mm1 and ins.op0_register == ins.op1_register:
                out.append(f"    {self.operand(ins,0,8)} = 0;"); return
            out.append(f"    {self.operand(ins,0,8)} = x_mmx_{mn}({self.operand(ins,0,8)}, {self.operand(ins,1,8)});"); return
        if mn in MMX_SHIFT and is_mm0:
            cnt = self.operand(ins, 1, 1) if ins.op1_kind == OpKind.IMMEDIATE8 else self.operand(ins, 1, 8)
            out.append(f"    {self.operand(ins,0,8)} = x_mmx_{mn}({self.operand(ins,0,8)}, (uint64_t){cnt});"); return

        # ---- SSE scalar subset --------------------------------------------------------
        if mn in ("movss", "movaps", "movups", "movlps", "movhps", "addss", "subss", "mulss", "divss", "sqrtss", "minss", "maxss",
                  "cvtsi2ss", "cvttss2si", "cvtss2si", "comiss", "ucomiss", "xorps", "andps", "orps", "addps", "subps", "mulps",
                  "shufps", "unpcklps", "movd", "rsqrtss", "rcpss"):
            self.lower_sse(ins, mn, out, U); return
        U()

    def trace_name(self, tgt: int) -> str:
        if tgt in self.hle: return self.hle[tgt]["name"]
        return KERNEL_EXPORTS.get(self.kthunks.get(tgt, 0), "?")

    def trace_argc(self, tgt: int) -> int:
        if tgt in self.hle: return sum(1 for a in self.hle[tgt].get("args", []) if a.startswith("psh"))
        return KERNEL_ARGC.get(KERNEL_EXPORTS.get(self.kthunks.get(tgt, 0), ""), 0)

    def call_expr(self, tgt: int) -> str:
        if tgt in self.hle:
            name = self.hle[tgt]["name"]
            self.hle_used.add(name)
            return f"XV_HLE_CALL(0x{tgt:X}u, xv_hle_{name})"
        if tgt in self.kthunks:
            name = KERNEL_EXPORTS.get(self.kthunks[tgt], f"ordinal_{self.kthunks[tgt]}")
            self.kernel_used.add(name)
            return f"XV_HLE_CALL(0x{tgt:X}u, xk_{name})"
        if tgt in self.disc.functions:
            return f"f_{tgt:08X}(c)"
        return f"xv_call(c, 0x{tgt:X}u)"

    # ---- x87 --------------------------------------------------------------------------
    def x87_mem(self, ins: Instruction) -> Tuple[str, str]:
        name = MSZ.get(ins.memory_size, "").lower()
        kind = X87_SIZES.get(name, None)
        return self.addr(ins), (kind or name)

    def lower_x87(self, ins, mn, out, U):
        def sti(i):
            return f"X_ST({REGNAME[ins.op_register(i)][2:].strip('()') or 0})"
        has_mem = ins.op_count and ins.op0_kind == OpKind.MEMORY
        if mn == "fld":
            if has_mem:
                a, k = self.x87_mem(ins); out.append(f"    x87_push(c, x87_load_{k}(c, {a}));")
            else:
                out.append(f"    x87_push(c, {sti(0)});")
            return
        if mn in ("fst", "fstp"):
            if has_mem:
                a, k = self.x87_mem(ins); out.append(f"    x87_store_{k}(c, {a}, X_ST(0));")
            else:
                out.append(f"    {sti(0)} = X_ST(0);")
            if mn == "fstp": out.append("    x87_pop(c);")
            return
        if mn in ("fild",):
            a, k = self.x87_mem(ins); out.append(f"    x87_push(c, x87_load_{k}(c, {a}));"); return
        if mn in ("fist", "fistp", "fisttp"):
            a, k = self.x87_mem(ins)
            out.append(f"    x87_store_{k}(c, {a}, {'x87_round' if mn != 'fisttp' else 'x87_trunc'}(c, X_ST(0)));")
            if mn != "fist": out.append("    x87_pop(c);")
            return
        CONSTS = {"fldz": "0.0", "fld1": "1.0", "fldpi": "3.14159265358979323846", "fldl2e": "1.44269504088896340736",
                  "fldln2": "0.693147180559945309417", "fldlg2": "0.301029995663981195214", "fldl2t": "3.32192809488736234787"}
        if mn in CONSTS:
            out.append(f"    x87_push(c, {CONSTS[mn]});"); return
        BIN = {"fadd": "+", "fsub": "-", "fmul": "*", "fdiv": "/", "fsubr": "-", "fdivr": "/",
               "faddp": "+", "fsubp": "-", "fmulp": "*", "fdivp": "/", "fsubrp": "-", "fdivrp": "/",
               "fiadd": "+", "fisub": "-", "fimul": "*", "fidiv": "/", "fisubr": "-", "fidivr": "/"}
        if mn in BIN:
            op = BIN[mn]; rev = "r" in mn[3:]
            pop = mn.endswith("p") and not mn.startswith("fi") or mn in ("faddp", "fsubp", "fmulp", "fdivp", "fsubrp", "fdivrp")
            if has_mem:
                a, k = self.x87_mem(ins); src = f"x87_load_{k}(c, {a})"; dst = "X_ST(0)"
            elif ins.op_count == 2:
                dst = sti(0); src = sti(1)
            else:
                dst = "X_ST(1)"; src = "X_ST(0)"
            if rev:
                out.append(f"    {dst} = {src} {op} {dst};")
            else:
                out.append(f"    {dst} = {dst} {op} {src};")
            if pop: out.append("    x87_pop(c);")
            return
        if mn == "fxch":
            # forms: fxch (implicit st1) / fxch st(i) (1 op) / fxch st, st(i) (2 ops - swap with the LAST operand)
            other = sti(ins.op_count - 1) if ins.op_count else "X_ST(1)"
            out.append(f"    {{ double t_ = X_ST(0); X_ST(0) = {other}; {other} = t_; }}"); return
        if mn in ("fcom", "fcomp", "fcompp", "fucom", "fucomp", "fucompp", "ficom", "ficomp", "fcomi", "fcomip", "fucomi", "fucomip"):
            if has_mem:
                a, k = self.x87_mem(ins); src = f"x87_load_{k}(c, {a})"
            elif ins.op_count:
                src = sti(ins.op_count - 1)           # fcom st(i) / fcomi st, st(i): compare with the LAST operand
            else:
                src = "X_ST(1)"
            eflags = mn.endswith("i") or mn.endswith("ip")
            out.append(f"    x87_compare(c, X_ST(0), {src}, {1 if eflags else 0});")
            pops = 2 if mn.endswith("pp") else (1 if mn.endswith("p") else 0)
            for _ in range(pops): out.append("    x87_pop(c);")
            return
        if mn == "ftst":
            out.append("    x87_compare(c, X_ST(0), 0.0, 0);"); return
        if mn == "fxam":
            out.append("    x87_fxam(c);"); return
        if mn in ("fnstsw", "fstsw"):
            if has_mem:
                out.append(f"    X_M16({self.addr(ins)}) = c->fsw;")
            else:
                out.append("    X_R16(0) = c->fsw;")
            return
        if mn in ("fnstcw", "fstcw"):
            out.append(f"    X_M16({self.addr(ins)}) = c->fcw;"); return
        if mn == "fldcw":
            out.append(f"    c->fcw = X_M16({self.addr(ins)});"); return
        if mn in ("fnclex", "fclex", "fninit", "finit", "fnop"):
            out.append("    c->fsw &= 0x7F00;" if "clex" in mn else "    x87_init(c);" if "init" in mn else "    ;"); return
        UN = {"fchs": "-X_ST(0)", "fabs": "fabs(X_ST(0))", "fsqrt": "sqrt(X_ST(0))", "fsin": "sin(X_ST(0))", "fcos": "cos(X_ST(0))",
              "frndint": "x87_round(c, X_ST(0))", "f2xm1": "(exp2(X_ST(0)) - 1.0)"}
        if mn in UN:
            out.append(f"    X_ST(0) = {UN[mn]};"); return
        if mn == "fsincos":
            out.append("    { double s_ = sin(X_ST(0)), c_ = cos(X_ST(0)); X_ST(0) = s_; x87_push(c, c_); }"); return
        if mn == "fptan":
            out.append("    X_ST(0) = tan(X_ST(0)); x87_push(c, 1.0);"); return
        if mn == "fpatan":
            out.append("    X_ST(1) = atan2(X_ST(1), X_ST(0)); x87_pop(c);"); return
        if mn == "fyl2x":
            out.append("    X_ST(1) = X_ST(1) * log2(X_ST(0)); x87_pop(c);"); return
        if mn == "fyl2xp1":
            out.append("    X_ST(1) = X_ST(1) * log2(X_ST(0) + 1.0); x87_pop(c);"); return
        if mn == "fscale":
            out.append("    X_ST(0) = ldexp(X_ST(0), (int)trunc(X_ST(1)));"); return
        if mn in ("fprem", "fprem1"):
            out.append("    X_ST(0) = fmod(X_ST(0), X_ST(1)); c->fsw &= ~0x0400;"); return
        if mn in ("ffree", "ffreep"):
            if mn == "ffreep": out.append("    x87_pop(c);")
            return
        if mn.startswith("fcmov"):
            cc = {"fcmovb": "b", "fcmove": "e", "fcmovbe": "be", "fcmovu": "p", "fcmovnb": "ae", "fcmovne": "ne", "fcmovnbe": "a", "fcmovnu": "np"}[mn]
            out.append(f"    if ({COND[cc]}) X_ST(0) = {sti(1)};"); return
        if mn in ("fnsave", "fsave", "frstor", "fnstenv", "fstenv", "fldenv", "fxsave", "fxrstor"):
            out.append(f"    xv_unimpl(c, 0x{ins.ip:X}u, \"{mn}\");"); self.unimpl[mn] += 1; return
        U()

    # ---- SSE ------------------------------------------------------------------------
    def lower_sse(self, ins, mn, out, U):
        def xmm(i): return f"c->xmm[{REGNAME[ins.op_register(i)][3:]}]"
        def is_xmm(i): return ins.op_kind(i) == OpKind.REGISTER and REGNAME[ins.op_register(i)].startswith("xmm")
        if mn == "movss":
            if is_xmm(0) and is_xmm(1):
                out.append(f"    {xmm(0)}[0] = {xmm(1)}[0];")
            elif is_xmm(0):
                out.append(f"    {xmm(0)}[0] = X_MF32({self.addr(ins)}); {xmm(0)}[1] = {xmm(0)}[2] = {xmm(0)}[3] = 0.0f;")
            else:
                out.append(f"    X_MF32({self.addr(ins)}) = {xmm(1)}[0];")
            return
        if mn in ("movaps", "movups"):
            if is_xmm(0) and is_xmm(1):
                out.append(f"    memcpy({xmm(0)}, {xmm(1)}, 16);")
            elif is_xmm(0):
                out.append(f"    x_load128(c, {xmm(0)}, {self.addr(ins)});")
            else:
                out.append(f"    x_store128(c, {self.addr(ins)}, {xmm(1)});")
            return
        if mn in ("movlps", "movhps"):
            lo = 0 if mn == "movlps" else 2
            if is_xmm(0):
                out.append(f"    {xmm(0)}[{lo}] = X_MF32({self.addr(ins)}); {xmm(0)}[{lo+1}] = X_MF32({self.addr(ins)}+4);")
            else:
                out.append(f"    X_MF32({self.addr(ins)}) = {xmm(1)}[{lo}]; X_MF32({self.addr(ins)}+4) = {xmm(1)}[{lo+1}];")
            return
        SS = {"addss": "+", "subss": "-", "mulss": "*", "divss": "/"}
        if mn in SS:
            src = f"{xmm(1)}[0]" if is_xmm(1) else f"X_MF32({self.addr(ins)})"
            out.append(f"    {xmm(0)}[0] = {xmm(0)}[0] {SS[mn]} {src};"); return
        PS = {"addps": "+", "subps": "-", "mulps": "*"}
        if mn in PS:
            if is_xmm(1):
                out.append(f"    for (int i_ = 0; i_ < 4; ++i_) {xmm(0)}[i_] = {xmm(0)}[i_] {PS[mn]} {xmm(1)}[i_];")
            else:
                out.append(f"    {{ float t_[4]; x_load128(c, t_, {self.addr(ins)}); for (int i_ = 0; i_ < 4; ++i_) {xmm(0)}[i_] = {xmm(0)}[i_] {PS[mn]} t_[i_]; }}")
            return
        if mn in ("sqrtss", "rsqrtss", "rcpss", "minss", "maxss"):
            src = f"{xmm(1)}[0]" if is_xmm(1) else f"X_MF32({self.addr(ins)})"
            expr = {"sqrtss": f"sqrtf({src})", "rsqrtss": f"1.0f / sqrtf({src})", "rcpss": f"1.0f / ({src})",
                    "minss": f"fminf({xmm(0)}[0], {src})", "maxss": f"fmaxf({xmm(0)}[0], {src})"}[mn]
            out.append(f"    {xmm(0)}[0] = {expr};"); return
        if mn == "cvtsi2ss":
            src = self.operand(ins, 1, 4)
            out.append(f"    {xmm(0)}[0] = (float)(int32_t){src};"); return
        if mn in ("cvttss2si", "cvtss2si"):
            src = f"{xmm(1)}[0]" if is_xmm(1) else f"X_MF32({self.addr(ins)})"
            out.append(f"    {self.operand(ins,0,4)} = (uint32_t)(int32_t){'truncf' if mn=='cvttss2si' else 'rintf'}({src});"); return
        if mn in ("comiss", "ucomiss"):
            src = f"{xmm(1)}[0]" if is_xmm(1) else f"X_MF32({self.addr(ins)})"
            out.append(f"    x_comiss(c, {xmm(0)}[0], {src});"); return
        if mn in ("xorps", "andps", "orps"):
            if is_xmm(1):
                if mn == "xorps" and ins.op0_register == ins.op1_register:
                    out.append(f"    memset({xmm(0)}, 0, 16);"); return
                out.append(f"    x_bitops128(c, {xmm(0)}, {xmm(1)}, '{ {'xorps':'^','andps':'&','orps':'|'}[mn] }');")
            else:
                out.append(f"    {{ float t_[4]; x_load128(c, t_, {self.addr(ins)}); x_bitops128(c, {xmm(0)}, t_, '{ {'xorps':'^','andps':'&','orps':'|'}[mn] }'); }}")
            return
        if mn == "shufps":
            imm = ins.immediate(2)
            if is_xmm(1):
                out.append(f"    x_shufps(c, {xmm(0)}, {xmm(1)}, {imm});")
            else:
                out.append(f"    {{ float t_[4]; x_load128(c, t_, {self.addr(ins)}); x_shufps(c, {xmm(0)}, t_, {imm}); }}")
            return
        if mn == "unpcklps":
            src = xmm(1) if is_xmm(1) else None
            if src:
                out.append(f"    x_unpcklps(c, {xmm(0)}, {src});"); return
        if mn == "movd":
            if is_xmm(0):
                out.append(f"    {{ uint32_t v_ = {self.operand(ins,1,4)}; memcpy(&{xmm(0)}[0], &v_, 4); {xmm(0)}[1] = {xmm(0)}[2] = {xmm(0)}[3] = 0.0f; }}")
            else:
                out.append(f"    memcpy(&{self.operand(ins,0,4)}, &{xmm(1)}[0], 4);")
            return
        U()

    # ---- functions / files ------------------------------------------------------------
    def dead_flag_writes(self, insns) -> set:
        """Backward liveness over one basic block: which instructions write flags that nothing reads
        before they are overwritten.  Conservative at the block end (successors may read: all live),
        except after call/ret, where x86 code never depends on flags."""
        RF_ALL = RflagsBits.OF | RflagsBits.SF | RflagsBits.ZF | RflagsBits.AF | RflagsBits.CF | RflagsBits.PF
        dead = set()
        if not insns:
            return dead
        RF_ALL2 = RF_ALL
        last = insns[-1]
        live = 0 if last.flow_control in (FlowControl.CALL, FlowControl.INDIRECT_CALL, FlowControl.RETURN) else RF_ALL2
        for ins in reversed(insns):
            rd = ins.rflags_read
            wr = ins.rflags_modified            # written | cleared | set | undefined
            if wr and not (wr & live) and not rd and ins.mnemonic not in FLAG_KEEP:
                dead.add(ins)
            live = (live & ~wr) | rd
        return dead

    def block_flag_use(self, insns) -> int:
        """Flags a block reads before writing them (plus whatever it leaves unwritten: conservative)."""
        RF_ALL = RflagsBits.OF | RflagsBits.SF | RflagsBits.ZF | RflagsBits.AF | RflagsBits.CF | RflagsBits.PF
        used = 0; written = 0
        for ins in insns:
            used |= ins.rflags_read & ~written
            if ins.flow_control in (FlowControl.CALL, FlowControl.INDIRECT_CALL, FlowControl.RETURN):
                return used                       # x86 code never keeps flags across calls/returns
            written |= ins.rflags_modified
            if written == RF_ALL:
                return used
        return used | (RF_ALL & ~written)

    def exit_live(self, fn, insns) -> int:
        """Flags live at the end of a block: the union of what its successors read first."""
        RF_ALL = RflagsBits.OF | RflagsBits.SF | RflagsBits.ZF | RflagsBits.AF | RflagsBits.CF | RflagsBits.PF
        if not insns:
            return RF_ALL
        last = insns[-1]
        fc = last.flow_control
        if fc in (FlowControl.CALL, FlowControl.INDIRECT_CALL, FlowControl.RETURN):
            return 0
        succ = []
        if fc in (FlowControl.CONDITIONAL_BRANCH, FlowControl.UNCONDITIONAL_BRANCH):
            succ.append(last.near_branch_target)
        if fc in (FlowControl.CONDITIONAL_BRANCH, FlowControl.NEXT):
            succ.append(last.next_ip)
        if fc == FlowControl.INDIRECT_BRANCH or not succ:
            return RF_ALL
        live = 0
        for t in succ:
            b = fn.blocks.get(t) if fn is not None else None
            if b is None:
                return RF_ALL
            live |= self.block_flag_use(b.insns)
        return live

    def fusable_pairs(self, insns, fn=None) -> dict:
        """index -> (kind, bits, cc) for flag-producing instructions whose flags are read only by the jcc
        that immediately follows (and are dead after it)."""
        RF_ALL = RflagsBits.OF | RflagsBits.SF | RflagsBits.ZF | RflagsBits.AF | RflagsBits.CF | RflagsBits.PF
        res = {}
        if len(insns) < 2:
            return res
        last = insns[-1]
        live_after = [0] * len(insns)
        # Successor-aware liveness (exit_live) mis-fused something (the CRT x87 exception path ran on the
        # host).  XBE_FUSE_EXT="lo-hi" enables it only for functions whose entry is in [lo, hi) (bisecting).
        ext = _os.environ.get("XBE_FUSE_EXT"); use_ext = False
        if ext and fn is not None:
            lo, hi = (int(x, 16) for x in ext.split("-")); use_ext = lo <= fn.entry < hi
        live = self.exit_live(fn, insns) if use_ext else (0 if last.flow_control in (FlowControl.CALL, FlowControl.INDIRECT_CALL, FlowControl.RETURN) else RF_ALL)
        for i in range(len(insns) - 1, -1, -1):
            live_after[i] = live
            ins = insns[i]
            live = (live & ~ins.rflags_modified) | ins.rflags_read
        for i in range(len(insns) - 1):
            a, j = insns[i], insns[i + 1]
            mn = MN[a.mnemonic]; jm = MN[j.mnemonic]
            if mn not in FUSE_MNEMONICS or not jm.startswith("j") or jm in ("jmp", "jecxz", "jcxz"):
                continue
            if a.rflags_read:                     # inc/dec keep CF: they read it - still fine, CF is not in the fused set
                pass
            if (a.rflags_modified & live_after[i + 1]) or not (j.rflags_read and (j.rflags_read & ~a.rflags_modified) == 0):
                continue
            cc = JCC_ALIAS.get(jm, jm)[1:]
            if cc in ("p", "np"):
                continue
            kind = "XK_LOGIC" if mn in ("test", "and", "or", "xor") else "XK_ADD" if mn in ("add", "inc") else "XK_SUB"
            if mn in ("inc", "dec") and cc in ("b", "ae", "be", "a"):
                continue                          # CF is preserved by inc/dec: not derivable from the captured operands
            bits = self.op_size(a, 0) * 8
            res[i] = (kind, bits, cc)
        return res

    def emit_function(self, fn: Function) -> str:
        # `restrict`: the context is host memory that no guest pointer can reach, so GCC may keep guest
        # registers in ARM registers across guest memory stores (otherwise every store reloads them).
        # xram_/xpt_: locals shadow the globals for the same reason (X_G is redefined per file to use them).
        out = [f"void f_{fn.entry:08X}(xctx *restrict c)", "{",
               "    uint8_t *const xram_ = g_xram; const uint32_t *const xpt_ = g_xpt; (void)xram_; (void)xpt_;"]
        self.cur_fn = fn.entry
        self._cur_fn_obj = fn
        if self.trace_funcs:
            out.append(f"    XV_FN(0x{fn.entry:08X}u);")
        # Blocks are emitted in address order.  When the function owns blocks below its entry (a jump
        # back into shared code, a tail merged with an earlier function) the first emitted block is
        # NOT the entry - without this goto the function would start executing someone else's code.
        # (Halo 3925: 790 of 8097 functions, e.g. game_time_initialize at 0xFA620 ran 0xF8140 instead.)
        if fn.blocks and min(fn.blocks) != fn.entry:
            out.append(f"    goto L_{fn.entry:08X};")
        out.append("    uint32_t fk_a = 0, fk_b = 0, fk_r = 0; (void)fk_a; (void)fk_b; (void)fk_r;")
        self.fused_cond = None
        for start in sorted(fn.blocks):
            blk = fn.blocks[start]
            out.append(f"L_{start:08X}:")
            dead = self.dead_flag_writes(blk.insns)
            fuse = self.fusable_pairs(blk.insns, fn)
            skip_next = False
            for idx, ins in enumerate(blk.insns):
                if skip_next:
                    skip_next = False; continue
                if idx in fuse:
                    kind, bits, cc = fuse[idx]
                    tmp: List[str] = []
                    self.lower(fn, ins, tmp)
                    cap = [FLAGS_CAP_RE.sub(lambda m: f"fk_a = (uint32_t)({m.group(2)}); fk_b = (uint32_t)({m.group(3)}); fk_r = (uint32_t)({m.group(4)}); ", line) for line in tmp]
                    out.extend(cap)
                    self.fused_cond = fused_cond(kind, bits, cc)
                    self.lower(fn, blk.insns[idx + 1], out)
                    self.fused_cond = None
                    self.stats["flags_fused"] = self.stats.get("flags_fused", 0) + 1
                    self.stats["insns"] += 2
                    skip_next = True
                    continue
                if ins in dead:
                    # flags this instruction writes are all overwritten before anything reads them:
                    # lower normally, then strip the X_FLAGS(...) store (7 stores per arithmetic op)
                    tmp: List[str] = []
                    self.lower(fn, ins, tmp)
                    out.extend(FLAGS_RE.sub("", line) for line in tmp)
                    self.stats["flags_elided"] = self.stats.get("flags_elided", 0) + 1
                else:
                    self.lower(fn, ins, out)
                self.stats["insns"] += 1
            # fallthrough into a block that is not the next in address order
            if blk.insns:
                last = blk.insns[-1]
                fc = last.flow_control
                if fc in (FlowControl.NEXT, FlowControl.CALL, FlowControl.INDIRECT_CALL) and blk.end in fn.blocks:
                    nxt_sorted = sorted(fn.blocks)
                    i = nxt_sorted.index(start)
                    if i + 1 >= len(nxt_sorted) or nxt_sorted[i + 1] != blk.end:
                        out.append(f"    goto L_{blk.end:08X};")
                elif fc == FlowControl.NEXT and blk.end not in fn.blocks:
                    out.append(f"    xv_trap(c, 0x{blk.end:X}u); return;   /* fell off the end */")
                elif fc == FlowControl.CONDITIONAL_BRANCH and blk.end in fn.blocks:
                    nxt_sorted = sorted(fn.blocks)
                    i = nxt_sorted.index(start)
                    if i + 1 >= len(nxt_sorted) or nxt_sorted[i + 1] != blk.end:
                        out.append(f"    goto L_{blk.end:08X};")
        out.append("    return;")
        out.append("}")
        return "\n".join(out)

    def write_all(self):
        os.makedirs(self.outdir, exist_ok=True)
        fns = sorted(self.disc.functions.values(), key=lambda f: f.entry)
        per = max(1, (len(fns) + self.nfiles - 1) // self.nfiles)
        proto = ["/* generated by xita_recomp.py */", "#pragma once", '#include "xv_x86rt.h"', ""]
        for f in fns:
            proto.append(f"void f_{f.entry:08X}(xctx *c);")
        for i in range(0, len(fns), per):
            chunk = fns[i:i + per]
            body = ['#include "xv_recomp_protos.h"',
                    "#undef X_G",
                    "#define X_G(a) ((void *)(xram_ + xpt_[(uint32_t)(a) >> 12] + ((uint32_t)(a) & 0xFFFu)))", ""]
            for f in chunk:
                body.append(self.emit_function(f))
                body.append("")
            with open(os.path.join(self.outdir, f"code_{i // per:03d}.c"), "w") as fh:
                fh.write("\n".join(body))
        # dispatch table
        tbl = ['#include "xv_recomp_protos.h"', "", "const xv_fn_entry_t xv_fn_table[] = {"]
        for f in fns:
            tbl.append(f"    {{ 0x{f.entry:08X}u, f_{f.entry:08X} }},")
        tbl.append("};")
        tbl.append(f"const unsigned xv_fn_table_count = {len(fns)};")
        tbl.append(f"const uint32_t xv_entry_point = 0x{self.img.entry:08X}u;")
        tbl.append(f"const uint32_t xv_game_main = 0x{self.game_main or 0:08X}u;")
        tbl.append(f"const uint32_t xv_game_tls_dir = 0x{self.img.tls:08X}u;")
        tbl.append(f"const uint32_t xv_d3d_g_pDevice_va = 0x{self.vars.get('D3D_g_pDevice', 0):08X}u;")
        tbl.append("typedef struct { uint32_t slot, ordinal; } xv_kimport_t;")
        tbl.append("const xv_kimport_t xv_kernel_imports[] = {")
        for slot, ordn in sorted(self.kthunks.items()):
            tbl.append(f"    {{ 0x{slot:08X}u, {ordn} }},   /* {KERNEL_EXPORTS.get(ordn, '?')} */")
        tbl.append("    { 0, 0 } };")
        tbl.append(f"const unsigned xv_kernel_imports_count = {len(self.kthunks)};")
        tbl.append("const char *const xv_kernel_names[367] = {")
        for o in range(367):
            tbl.append(f'    "{KERNEL_EXPORTS.get(o, "")}",')
        tbl.append("};")
        for o in sorted(set(self.kthunks.values())):
            tbl.append(f"void xk_{KERNEL_EXPORTS.get(o, f'ordinal_{o}')}(xctx *c) __attribute__((weak));")
        tbl.append("xv_fn_t xv_kernel_dispatch(unsigned ordinal) {")
        tbl.append("    switch (ordinal) {")
        for o in sorted(set(self.kthunks.values())):
            tbl.append(f"    case {o}: return xk_{KERNEL_EXPORTS.get(o, f'ordinal_{o}')};")
        tbl.append("    default: return 0; } }")
        tbl.append("const xv_fn_entry_t xv_hle_table[] = {")
        for addr, s in sorted(self.hle.items()):
            if s["name"] in self.hle_used:
                tbl.append(f"    {{ 0x{addr:08X}u, xv_hle_{s['name']} }},")
        tbl.append("    { 0, 0 } };")
        tbl.append(f"const unsigned xv_hle_table_count = {sum(1 for s in self.hle.values() if s['name'] in self.hle_used)};")
        # HLE + kernel prototypes
        proto.append("")
        proto.append("extern int xv_trace_enabled; void xv_trace_call(xctx *c, const char *name, unsigned nargs);")
        proto.append("extern int xv_trace_funcs; void xv_trace_func(uint32_t entry);   /* --trace-funcs: per-frame call histogram */")
        proto.append("extern volatile uint32_t xv_cur_fn;                       /* sampling profiler: last entered function */")
        proto.append("/* HLE/kernel calls mark the profiler's current function as 0x80000000 | guest address of the HLE, so runtime time")
        proto.append(" * (draw translation, file I/O, waits) is its own bucket instead of landing on the last guest function entered. */")
        proto.append("#define XV_HLE_CALL(addr, fn) do { uint32_t xvfn_ = xv_cur_fn; xv_cur_fn = 0x80000000u | (uint32_t)(addr); fn(c); xv_cur_fn = xvfn_; } while (0)")
        proto.append("extern int xv_watch_n; void xv_watch_enter(uint32_t fn, xctx *c); void xv_watch_leave(uint32_t fn, uint32_t back, xctx *c);\n#define XV_FN(a) do { xv_cur_fn = (a); if (xv_trace_funcs) xv_trace_func(a); if (xv_watch_n) xv_watch_enter((a), c); } while (0)")
        proto.append("#define XV_FN_BACK(a) do { if (xv_watch_n) xv_watch_leave(xv_cur_fn, (a), c); xv_cur_fn = (a); } while (0)")
        proto.append("/* HLE symbols (weak: default trap until implemented) */")
        for name in sorted(self.hle_used):
            proto.append(f"void xv_hle_{name}(xctx *c) __attribute__((weak));")
        proto.append("/* kernel exports */")
        for name in sorted(self.kernel_used):
            proto.append(f"void xk_{name}(xctx *c) __attribute__((weak));")
        with open(os.path.join(self.outdir, "xv_recomp_protos.h"), "w") as fh:
            fh.write("\n".join(proto) + "\n")
        with open(os.path.join(self.outdir, "xv_fn_table.c"), "w") as fh:
            fh.write("\n".join(tbl) + "\n")
        # stub file: weak defaults for HLE/kernel names so the image links.  They trace the
        # call (name + stack args), return 0 in eax and pop stdcall arguments; real
        # implementations (strong symbols) override them at link time.
        argc = {}
        for s in self.hle.values():
            argc[s["name"]] = sum(1 for a in s.get("args", []) if a.startswith("psh"))
        stubs = ['#include "xv_recomp_protos.h"', "void xv_trace_call(xctx *c, const char *name, unsigned nargs);", ""]
        for name in sorted(self.hle_used):
            n = argc.get(name, 0)
            stubs.append(f'void xv_hle_{name}(xctx *c) {{ xv_trace_call(c, "{name}", {n}); c->r[0] = 0; X_RET({n}); }}')
        for name in sorted(self.kernel_used):
            n = KERNEL_ARGC.get(name, 0)
            stubs.append(f'void xk_{name}(xctx *c) {{ xv_trace_call(c, "{name}", {n}); c->r[0] = 0; X_RET({n}); }}')
        with open(os.path.join(self.outdir, "xv_stubs_default.c"), "w") as fh:
            fh.write("\n".join(stubs) + "\n")
        return len(fns)


# --------------------------------------------------------------------------------------

# XAPI helpers that are pure guest-side bookkeeping (TLS/KTHREAD fields, object-attribute
# formatting): more faithful to recompile them than to reimplement them.
DEFAULT_LIFT = {"SetLastError", "GetLastError", "XapiSetLastNTError", "XapiFormatObjectAttributes",
                "XapiMapLetterToDirectory", "XapiSelectCachePartition", "XGetSectionSize", "GetTimeZoneInformation",
                "RaiseException", "UnhandledExceptionFilter", "XAutoPowerDownResetTimer", "XRegisterThreadNotifyRoutine",
                # the real XAPI start-up chain and thread/sync wrappers: they only need the kernel underneath
                "mainCRTStartup", "mainXapiStartup", "XapiInitProcess", "XapiBootToDash", "CreateThread", "CreateEventA",
                "CreateMutexA", "SetEvent", "ResetEvent", "SwitchToThread", "SetThreadPriority", "GetExitCodeThread",
                "XapiThreadStartup", "MU_Init", "XapiInitDefaultHeap", "XapiHeapAlloc"}


# Libraries that are replaced wholesale by the Vita runtime, and the XAPI entry points that talk to
# hardware (USB input, launch data, debug output) and therefore stay HLE even though XAPILIB is lifted.
HLE_LIBS = {"D3D8", "D3DX", "DSOUND", "XNETS", "XONLINE", "XGRAPHC", "XACTENG"}
HLE_KEEP = {"XGetLaunchInfo", "XInitDevices", "XGetDeviceChanges", "XInputOpen", "XInputClose", "XInputGetState",
            "XInputSetState", "XInputPoll", "XLaunchNewImageA", "OutputDebugStringA", "XCalculateSignatureBegin",
            "XCalculateSignatureUpdate", "XCalculateSignatureEnd", "MU_Init", "XMountMUA", "XUnmountMU", "XMountUtilityDrive",
            "XSetProcessQuantumLength", "XGetDevices"}


def find_main(img: Image, hle: Dict[int, dict]) -> Optional[int]:
    """XAPI: mainCRTStartup -> CreateThread(mainXapiStartup) -> XapiInitProcess, _rtinit, _cinit, main(), XapiBootToDash."""
    start = next((a for a, s in hle.items() if s["name"] == "mainXapiStartup"), None)
    if start is None:
        return None
    ip = start
    for _ in range(64):
        b = img.bytes_at(ip, 16)
        if not b:
            break
        ins = Decoder(32, b, ip=ip).decode()
        if ins.is_invalid:
            break
        if ins.flow_control == FlowControl.CALL and ins.near_branch_target not in hle and img.is_code(ins.near_branch_target):
            return ins.near_branch_target
        if ins.flow_control == FlowControl.RETURN:
            break
        ip = ins.next_ip
    return None


def main() -> int:
    ap = argparse.ArgumentParser(prog="xita_recomp.py")
    ap.add_argument("xbe")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--symbols", help="XbSymbolDatabase JSON (halo_symbols.json)")
    ap.add_argument("-o", "--outdir", default="recomp")
    ap.add_argument("--files", type=int, default=32, help="number of .c files to split into")
    ap.add_argument("--roots", nargs="*", default=[], help="extra root addresses (hex); first one is treated as main()")
    ap.add_argument("--no-data-roots", action="store_true", help="do not treat code pointers in data sections as roots")
    ap.add_argument("--lift", nargs="*", default=[], help="symbol names to recompile instead of HLE (XAPI internals)")
    ap.add_argument("--trace-calls", action="store_true", help="instrument kernel/HLE call sites (runtime flag xv_trace_enabled)")
    ap.add_argument("--trace-funcs", action="store_true", help="count every recompiled function entry (runtime: XV_FUNC_HIST=<frame> dumps that frame's histogram)")
    ap.add_argument("--hle-addr", nargs="*", default=[], help="extra HLE overrides by address: HEXADDR:Name:StackArgc")
    args = ap.parse_args()

    img = Image(args.xbe, args.manifest)
    hle: Dict[int, dict] = {}
    lift = set(args.lift) | DEFAULT_LIFT
    if args.symbols:
        for s in json.load(open(args.symbols)):
            if s["kind"] != "FUN" or s["name"] in lift:
                continue
            if s["lib"] not in HLE_LIBS and s["name"] not in HLE_KEEP:
                continue                                    # XAPILIB etc.: recompile the original code
            hle[s["address"]] = s
    for spec in args.hle_addr:
        addr, name, argc = spec.split(":")
        hle[int(addr, 16)] = {"lib": "CUSTOM", "kind": "FUN", "convention": "stdcall", "name": name,
                              "args": ["psh a%d" % i for i in range(int(argc))], "address": int(addr, 16)}
    kthunks = img.kernel_imports()
    print(f"entry 0x{img.entry:08X}; kernel thunk 0x{img.kernel_thunk:08X} with {len(kthunks)} imports; {len(hle)} HLE functions")

    log = print
    disc = Discovery(img, hle, kthunks, log)
    disc.add_root(img.entry)
    game_main = None
    for r in args.roots:
        disc.add_root(int(r, 16))
        game_main = game_main or int(r, 16)
    if not args.roots:
        game_main = find_main(img, hle)
        if game_main:
            print(f"game main() = 0x{game_main:08X} (first non-symbol call from mainXapiStartup)")
            disc.add_root(game_main)
    if not args.no_data_roots:
        ptrs = img.data_code_pointers()
        disc.candidates.update(ptrs)
        print(f"{len(ptrs)} code pointers in data sections queued as candidate roots")
    disc.run()
    nblocks = sum(len(f.blocks) for f in disc.functions.values())
    ninsn = sum(len(b.insns) for f in disc.functions.values() for b in f.blocks.values())
    print(f"discovered {len(disc.functions)} functions, {nblocks} blocks, {ninsn:,} instructions "
          f"({sum(len(t) for f in disc.functions.values() for t in f.switch_tables.values())} switch targets); {dict(disc.stats)}")

    em = Emitter(img, disc, hle, kthunks, args.outdir, args.files)
    em.game_main = game_main
    em.trace = args.trace_calls
    em.trace_funcs = args.trace_funcs
    if args.symbols:
        em.vars = {s["name"]: s["address"] for s in json.load(open(args.symbols)) if s["kind"] == "VAR"}
    n = em.write_all()
    total = em.stats["insns"]
    un = sum(em.unimpl.values())
    print(f"emitted {n} functions / {total:,} instructions to {args.outdir}/ ; unimplemented {un} ({100.0*un/max(total,1):.2f}%)")
    print("  unimplemented by mnemonic:", " ".join(f"{k}:{v}" for k, v in em.unimpl.most_common(40)))
    print(f"  HLE symbols referenced: {len(em.hle_used)}; kernel exports referenced: {len(em.kernel_used)}")
    kernel_names = sorted(em.kernel_used)
    print("  kernel:", " ".join(kernel_names))
    json.dump({"functions": [f.entry for f in sorted(disc.functions.values(), key=lambda f: f.entry)],
               "hle_used": sorted(em.hle_used), "kernel_used": kernel_names,
               "unimplemented": dict(em.unimpl)}, open(os.path.join(args.outdir, "recomp_report.json"), "w"), indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
