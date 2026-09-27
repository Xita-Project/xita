"""Opt-in x87 register-stack codegen (xita_recomp.py --x87-regs).

The default lowering keeps the x87 stack in the guest context: X_ST(i) is
c->st[(c->fsp + i) & 7] and x87_push/x87_pop move c->fsp, so every operand is a
dynamically indexed load or store through the context and GCC can keep no x87
value in a VFP register.

This mode tracks the x87 stack depth statically. Inside one function the depth
d is the number of net pushes since entry; st(i) at depth d is the logical slot
L = d - 1 - i (the first push is slot 0, the caller's st(0) is slot -1). Each
slot the function touches becomes a C double local that mirrors the physical
entry c->st[(fsp0 - 1 - L) & 7], where fsp0 is c->fsp at entry. The status word
c->fsw becomes a uint16_t local as well; c->fcw stays in memory.

Memory is brought back in sync at every point where something else can look at
it (sync points): calls to guest functions, HLE and kernel calls, returns, tail
calls, and scheduler yields. Before a sync the slots written since the last sync (all of them,
including values already popped, so c->st[] holds exactly what the memory
lowering would have left there), c->fsp and c->fsw are stored; after a call
every slot and c->fsw are reloaded, because the callee may have written any of
them. The arithmetic itself is the memory lowering's, expression for
expression, so values and the status-word side effects are unchanged.

A function is converted only when the depth is statically consistent: every
join point is reached with one depth, every call has a known x87 effect
(interprocedural summaries over direct callees; HLE/kernel effects from
HLE_X87_DELTA; indirect calls are unknown), no instruction inspects or resets
the stack as a whole (fninit, fsave/frstor/fstenv/fldenv, fincstp/fdecstp, any
mnemonic the lowering does not know), all touched slots fit the 8 physical
registers, every instruction is reached, and game hooks do not reach into the
body. Anything else falls back to the memory lowering for the whole function;
the report says why.
"""
from __future__ import annotations

import difflib
import re
from typing import Dict, List, Optional, Tuple

from iced_x86 import FlowControl, OpKind, Register

# HLE implementations that move the x87 stack (every other HLE and kernel export leaves c->st/c->fsp
# alone; tools/check_x87_hle.py re-checks this against recomp/kernel and runtime sources).
HLE_X87_DELTA = {"crt_fmod": -1}      # xv_hle_crt_fmod: st0 = fmod(st1, st0) with one pop (xk_xapi.c)

TOP = "top"          # summary not known yet / never returns
BOT = "bottom"       # inconsistent or unknowable x87 effect

X87_UNSUPPORTED = {"fninit", "finit", "fnsave", "fsave", "frstor", "fnstenv", "fstenv", "fldenv",
                   "fxsave", "fxrstor", "fincstp", "fdecstp", "fstpnce", "fbld", "fbstp", "fsetpm",
                   "fneni", "fndisi", "feni", "fdisi", "frstpm", "fstp1", "fstp8", "fstp9", "fxch4", "fxch7",
                   "fcom2", "fcomp3", "fcomp5", "ffreep"}

# Lines a game transform_body hook may insert into a converted function: pure observers that never
# touch the x87 state (light census, hold profile, phase timers).
OBSERVER_LINE = re.compile(
    r"^\s*(#if.*|#ifdef \w+|#else|#endif|\}|\{"
    r"|XV_LIGHT_CENSUS_\w+\(c\);|XV_QUERY_WORK_(BEGIN|END)\(c, xv_object_math_locked_\);"
    r"|\{ extern unsigned xv_object_hold_children_enabled;|extern unsigned xv_object_hold_child_begin\(int\);"
    r"|extern void xv_object_hold_child_end\(unsigned,unsigned\);|unsigned hold_child_ = xv_object_hold_children_enabled \?"
    r"|xv_object_hold_child_begin\(xv_object_math_locked_\) : 0;|if \(hold_child_\) xv_object_hold_child_end\(hold_child_,\d+\);"
    r"|f_[0-9A-F]{8}\(c\);"
    r"|\{ extern void xv_scene_phase_(begin|end)\(uint32_t\); xv_scene_phase_(begin|end)\(0x[0-9A-F]+u\); \})\s*$")


class Unsupported(Exception):
    pass


def parse_entries(text):
    """'8D760,0x92330' -> {0x8D760, 0x92330}; None/'' -> None."""
    if not text:
        return None
    return {int(x, 16) for x in text.split(",") if x.strip()}


def slot_name(L: int, physical: bool = False) -> str:
    if physical:
        L &= 7
    return f"xr{L}" if L >= 0 else f"xrn{-L}"


def phys(L: int) -> str:
    """c->st index of logical slot L: (fsp0 - 1 - L) & 7."""
    return f"(xfsp0 + {(-1 - L) & 7}u) & 7u"


class Ctx:
    """Lowering state for one instruction: current depth and the slots/status word it touches."""
    def __init__(self, d: int, physical: bool = False):
        self.physical = physical
        self.d = d
        self.used = set()
        self.written = set()
        self.fsw_read = False
        self.fsw_written = False

    def _slot(self, i: int) -> int:
        L = self.d - 1 - i
        self.used.add(L)
        return L

    def st(self, i: int) -> str:
        return slot_name(self._slot(i), self.physical)

    def stw(self, i: int) -> str:
        L = self._slot(i)
        self.written.add(L)
        return slot_name(L, self.physical)

    def push(self, expr: str) -> str:
        L = self.d
        self.used.add(L)
        self.written.add(L)
        self.d += 1
        return f"{slot_name(L, self.physical)} = {expr};"

    def pop(self):
        self.d -= 1

    def top(self) -> str:
        return f"((xfsp0 + {(-self.d) & 7}u) & 7u)"

    def fsw(self, write: bool = False) -> str:
        self.fsw_read = True
        if write:
            self.fsw_written = True
        return "xfsw"


def lower_x87_regs(em, ins, mn: str, ctx: Ctx, COND: dict) -> List[str]:
    """Register-slot twin of Emitter.lower_x87: the same C expressions, with X_ST(i) -> slot locals,
    x87_push/pop -> static depth, c->fsw -> xfsw and x87_compare/x87_fxam -> the xv_x87reg.h twins."""
    from recompiler.xita_recomp import REGNAME, OpKind as _OK
    out: List[str] = []

    def reg(i: int) -> int:
        return int(REGNAME[ins.op_register(i)][2:].strip('()') or 0)

    has_mem = ins.op_count and ins.op0_kind == OpKind.MEMORY
    if mn in X87_UNSUPPORTED:
        raise Unsupported(mn)
    if mn == "fld":
        if has_mem:
            a, k = em.x87_mem(ins); out.append(f"    {ctx.push(f'x87_load_{k}(c, {a})')}")
        else:
            out.append(f"    {ctx.push(ctx.st(reg(0)))}")
        return out
    if mn in ("fst", "fstp"):
        if has_mem:
            a, k = em.x87_mem(ins); out.append(f"    x87_store_{k}(c, {a}, {ctx.st(0)});")
        else:
            src = ctx.st(0); dst = ctx.stw(reg(0))
            out.append(f"    {dst} = {src};")
        if mn == "fstp": ctx.pop()
        return out
    if mn == "fild":
        a, k = em.x87_mem(ins); out.append(f"    {ctx.push(f'x87_load_{k}(c, {a})')}"); return out
    if mn in ("fist", "fistp", "fisttp"):
        a, k = em.x87_mem(ins)
        out.append(f"    x87_store_{k}(c, {a}, {'x87_round' if mn != 'fisttp' else 'x87_trunc'}(c, {ctx.st(0)}));")
        if mn != "fist": ctx.pop()
        return out
    CONSTS = {"fldz": "0.0", "fld1": "1.0", "fldpi": "3.14159265358979323846", "fldl2e": "1.44269504088896340736",
              "fldln2": "0.693147180559945309417", "fldlg2": "0.301029995663981195214", "fldl2t": "3.32192809488736234787"}
    if mn in CONSTS:
        out.append(f"    {ctx.push(CONSTS[mn])}"); return out
    BIN = {"fadd": "+", "fsub": "-", "fmul": "*", "fdiv": "/", "fsubr": "-", "fdivr": "/",
           "faddp": "+", "fsubp": "-", "fmulp": "*", "fdivp": "/", "fsubrp": "-", "fdivrp": "/",
           "fiadd": "+", "fisub": "-", "fimul": "*", "fidiv": "/", "fisubr": "-", "fidivr": "/"}
    if mn in BIN:
        op = BIN[mn]; rev = "r" in mn[3:]
        pop = mn.endswith("p") and not mn.startswith("fi") or mn in ("faddp", "fsubp", "fmulp", "fdivp", "fsubrp", "fdivrp")
        if has_mem:
            a, k = em.x87_mem(ins); src = f"x87_load_{k}(c, {a})"; di = 0
        elif ins.op_count == 2:
            di = reg(0); src = ctx.st(reg(1))
        else:
            di = 1; src = ctx.st(0)
        dst = ctx.stw(di)
        if rev:
            out.append(f"    {dst} = {src} {op} {dst};")
        else:
            out.append(f"    {dst} = {dst} {op} {src};")
        if pop: ctx.pop()
        return out
    if mn == "fxch":
        a = ctx.stw(0); b = ctx.stw(reg(ins.op_count - 1) if ins.op_count else 1)
        out.append(f"    {{ double t_ = {a}; {a} = {b}; {b} = t_; }}"); return out
    if mn in ("fcom", "fcomp", "fcompp", "fucom", "fucomp", "fucompp", "ficom", "ficomp", "fcomi", "fcomip", "fucomi", "fucomip"):
        if has_mem:
            a, k = em.x87_mem(ins); src = f"x87_load_{k}(c, {a})"
        elif ins.op_count:
            src = ctx.st(reg(ins.op_count - 1))
        else:
            src = ctx.st(1)
        eflags = mn.endswith("i") or mn.endswith("ip")
        out.append(f"    {ctx.fsw(True)} = x87r_compare(c, xfsw, {ctx.st(0)}, {src}, {1 if eflags else 0}, {ctx.top()});")
        pops = 2 if mn.endswith("pp") else (1 if mn.endswith("p") else 0)
        for _ in range(pops): ctx.pop()
        return out
    if mn == "ftst":
        out.append(f"    {ctx.fsw(True)} = x87r_compare(c, xfsw, {ctx.st(0)}, 0.0, 0, {ctx.top()});"); return out
    if mn == "fxam":
        out.append(f"    {ctx.fsw(True)} = x87r_fxam(xfsw, {ctx.st(0)});"); return out
    if mn in ("fnstsw", "fstsw"):
        if has_mem:
            out.append(f"    X_M16({em.addr(ins)}) = {ctx.fsw()};")
        else:
            out.append(f"    X_R16(0) = {ctx.fsw()};")
        return out
    if mn in ("fnstcw", "fstcw"):
        out.append(f"    X_M16({em.addr(ins)}) = c->fcw;"); return out
    if mn == "fldcw":
        out.append(f"    c->fcw = X_M16({em.addr(ins)});"); return out
    if mn in ("fnclex", "fclex"):
        out.append(f"    {ctx.fsw(True)} &= 0x7F00;"); return out
    if mn == "fnop":
        out.append("    ;"); return out
    UN = {"fchs": "-{0}", "fabs": "fabs({0})", "fsqrt": "sqrt({0})", "fsin": "sin({0})", "fcos": "cos({0})",
          "frndint": "x87_round(c, {0})", "f2xm1": "(exp2({0}) - 1.0)"}
    if mn in UN:
        s = ctx.stw(0)
        out.append(f"    {s} = {UN[mn].format(s)};"); return out
    if mn == "fsincos":
        s = ctx.stw(0)
        out.append(f"    {{ double s_ = sin({s}), c_ = cos({s}); {s} = s_; {ctx.push('c_')} }}"); return out
    if mn == "fptan":
        s = ctx.stw(0)
        out.append(f"    {s} = tan({s}); {ctx.push('1.0')}"); return out
    if mn == "fpatan":
        s1 = ctx.stw(1); s0 = ctx.st(0)
        out.append(f"    {s1} = atan2({s1}, {s0});"); ctx.pop(); return out
    if mn == "fyl2x":
        s1 = ctx.stw(1); s0 = ctx.st(0)
        out.append(f"    {s1} = {s1} * log2({s0});"); ctx.pop(); return out
    if mn == "fyl2xp1":
        s1 = ctx.stw(1); s0 = ctx.st(0)
        out.append(f"    {s1} = {s1} * log2({s0} + 1.0);"); ctx.pop(); return out
    if mn == "fscale":
        s0 = ctx.stw(0); s1 = ctx.st(1)
        out.append(f"    {s0} = ldexp({s0}, (int)trunc({s1}));"); return out
    if mn in ("fprem", "fprem1"):
        s0 = ctx.stw(0); s1 = ctx.st(1)
        out.append(f"    {s0} = fmod({s0}, {s1}); {ctx.fsw(True)} &= ~0x0400;"); return out
    if mn == "ffree":
        return out
    if mn.startswith("fcmov"):
        cc = {"fcmovb": "b", "fcmove": "e", "fcmovbe": "be", "fcmovu": "p", "fcmovnb": "ae", "fcmovne": "ne", "fcmovnbe": "a", "fcmovnu": "np"}[mn]
        src = ctx.st(reg(1)); dst = ctx.stw(0)
        out.append(f"    if ({COND[cc]}) {dst} = {src};"); return out
    raise Unsupported(mn)


class State:
    __slots__ = ("d", "mem", "dirty", "fswd")

    def __init__(self, d, mem, dirty, fswd):
        self.d, self.mem, self.dirty, self.fswd = d, mem, dirty, fswd

    def key(self):
        return (self.d, self.mem, self.dirty, self.fswd)


class Plan:
    """Everything the emitter needs for one converted function."""
    def __init__(self, fn):
        self.entry = fn.entry
        self.physical = False
        self.states: Dict[Tuple[int, int], State] = {}     # (block start, index) -> state before the instruction
        self.final_return: Optional[State] = None          # state reaching emit_function's trailing return
        self.guards: Dict[Tuple[int, int], Tuple[int, int]] = {}   # guarded call -> (call ip, assumed depth after)
        self.lo = 0
        self.hi = -1
        self.fsw = False
        self.x87 = 0
        self.syncs = 0

    @property
    def slots(self):
        slots = range(self.lo, self.hi + 1)
        return sorted({L & 7 for L in slots}) if self.physical else slots

    # ---- emitted text ---------------------------------------------------------------
    def prologue(self) -> List[str]:
        out = ["    /* --x87-regs: x87 slots live in locals (recompiler/x87_regs.py); c->st/c->fsp/c->fsw synced at calls and exits */",
               "    const uint32_t xfsp0 = c->fsp; (void)xfsp0;"]
        if self.hi >= self.lo:
            out.append("    double " + ", ".join(f"{slot_name(L, self.physical)} = c->st[{phys(L)}]" for L in self.slots) + ";")
        if self.fsw:
            out.append("    uint16_t xfsw = c->fsw;")
        return out

    def spill(self, s: State) -> str:
        parts = [f"c->st[{phys(L)}] = {slot_name(L, self.physical)};" for L in sorted({L & 7 for L in s.dirty} if self.physical else s.dirty)]
        if s.mem != s.d:
            parts.append(f"c->fsp = (xfsp0 + {(-s.d) & 7}u) & 7u;")
        if s.fswd:
            parts.append("c->fsw = xfsw;")
        return " ".join(parts)

    def fill(self) -> str:
        parts = [f"{slot_name(L, self.physical)} = c->st[{phys(L)}];" for L in self.slots]
        if self.fsw:
            parts.append("xfsw = c->fsw;")
        return " ".join(parts)

    def guard(self, pos) -> str:
        """After a call whose x87 effect is assumed, not proven: if c->fsp says otherwise, continue in the
        memory-lowering copy of this function (exact by construction: memory is authoritative right after
        a call). Empty for calls with a proven effect."""
        g = self.guards.get(pos)
        if g is None:
            return ""
        ip, d = g
        return (f"if (__builtin_expect(c->fsp != ((xfsp0 + {(-d) & 7}u) & 7u), 0)) "
                f"{{ if (xv_x87reg_miss) xv_x87reg_miss(c, 0x{ip:X}u); goto {resume_label(pos)}; }}")


def resume_label(pos) -> str:
    return f"M_{pos[0]:08X}_{pos[1]}"


class X87Regs:
    """Static x87 depth analysis over all discovered functions, then per-function plans.

    Two interprocedural summaries: `proven` (every path's effect known: direct callees, HLE/kernel table;
    an indirect call anywhere below makes it BOT) and `assumed` (indirect calls and unknown callees take
    the effect the code after the call expects, see guess()). Calls whose effect is only assumed get a
    runtime guard in the converted caller; everything proven is used without checks."""

    def __init__(self, em, only=None, exclude=None, guards=True, min_density=0.0, physical_slots=None):
        self.em = em
        self.physical_slots = set(physical_slots or ())
        self.min_density = min_density     # x87 instructions per sync point below which the memory lowering stays
        self.disc = em.disc
        self.only = only
        self.exclude = exclude or set()
        self.use_guards = guards
        self.proven: Dict[int, object] = {}
        self.assumed: Dict[int, object] = {}
        self.plans: Dict[int, Plan] = {}
        self.reasons: Dict[int, str] = {}
        self.x87_count: Dict[int, int] = {}
        self._classes: Dict[int, list] = {}
        self.rounds = 0

    # ---- instruction classes (mirror Emitter.lower's control-flow lowering) -----------
    def target_kind(self, tgt: int):
        em = self.em
        if tgt in em.hle:
            return ("hle", em.hle[tgt]["name"])
        if tgt in em.kthunks:
            return ("k", tgt)
        if tgt in self.disc.functions:
            return ("fn", tgt)
        return ("ind",)

    def classify(self, fn, ins):
        from recompiler.xita_recomp import MN
        mn = MN[ins.mnemonic]
        if mn == "call":
            if ins.op0_kind == OpKind.NEAR_BRANCH32:
                return ("call", self.target_kind(ins.near_branch_target))
            if ins.op0_kind == OpKind.MEMORY and ins.memory_base == Register.NONE and ins.memory_index == Register.NONE:
                if (ins.memory_displacement & 0xFFFFFFFF) in self.em.kthunks:
                    return ("call", ("k", 0))
            return ("call", ("ind",))
        if mn == "ret":
            return ("ret",)
        if mn == "jmp":
            if ins.op0_kind == OpKind.NEAR_BRANCH32:
                tgt = ins.near_branch_target
                return ("goto", tgt) if tgt in fn.blocks else ("tail", self.target_kind(tgt))
            if ins.ip in fn.switch_tables:
                return ("switch", [t for _, t in fn.switch_tables[ins.ip]])
            return ("tail", ("ind",))
        if (mn.startswith("j") and mn != "jmp") or mn in ("loop", "loope", "loopne"):
            tgt = ins.near_branch_target
            return ("br", tgt) if tgt in fn.blocks else ("ctail", self.target_kind(tgt))
        if mn.startswith("f") and mn != "fs" and mn not in ("fwait", "femms"):
            return ("x87", mn)
        if mn in ("int3", "hlt"):
            return ("trap",)
        return ("other",)

    def classes(self, fn):
        c = self._classes.get(fn.entry)
        if c is None:
            c = {s: [self.classify(fn, ins) for ins in fn.blocks[s].insns] for s in fn.blocks}
            self._classes[fn.entry] = c
        return c

    # ---- call effects ------------------------------------------------------------------
    def table_delta(self, kind):
        if kind[0] == "hle":
            return HLE_X87_DELTA.get(kind[1], 0)
        if kind[0] == "k":
            return 0
        return None

    def guess(self, fn, start, idx, d):
        """Effect the caller's own code expects from the call at (start, idx): MSVC returns float/double in
        st(0) and callers consume it before pushing anything, so +1 when the next x87 instruction (same
        block, then fallthrough) reads below the pre-call depth, else 0."""
        cls = self.classes(fn)
        blk = fn.blocks[start]
        i = idx + 1
        steps = 0
        while steps < 64:
            if i >= len(blk.insns):
                nxt = blk.end
                if nxt not in fn.blocks or not blk.insns or blk.insns[-1].flow_control not in (FlowControl.NEXT, FlowControl.CALL, FlowControl.INDIRECT_CALL):
                    return 0
                start, blk, i = nxt, fn.blocks[nxt], 0
                continue
            c = cls[start][i]
            if c[0] == "x87":
                ctx = Ctx(d)
                try:
                    lower_x87_regs(self.em, blk.insns[i], c[1], ctx, _COND())
                except Unsupported:
                    return 0
                return 1 if ctx.used and min(ctx.used) < d else 0
            if c[0] != "other":
                return 0
            i += 1; steps += 1
        return 0

    def call_effect(self, fn, start, idx, kind, d, mode):
        """(delta, guarded) for a call in `mode`; delta may be TOP/BOT in the summary modes."""
        t = self.table_delta(kind)
        if t is not None:
            return t, False
        if kind[0] == "fn":
            p = self.proven.get(kind[1], TOP)
            if isinstance(p, int):
                return p, False
            if mode == "proven":
                return p, False
            a = self.assumed.get(kind[1], TOP)
            if isinstance(a, int):
                return a, True
            if mode == "assumed" and a == TOP:
                return TOP, True               # not computed yet (or never returns): pending
        elif mode == "proven":
            return BOT, False
        return self.guess(fn, start, idx, d), True

    # ---- per-function dataflow ---------------------------------------------------------
    def block_exit(self, fn, order, i):
        """(successors, falls_to_final_return) after block order[i], mirroring emit_function."""
        start = order[i]
        blk = fn.blocks[start]
        if not blk.insns:
            nxt = order[i + 1] if i + 1 < len(order) else None
            return ([nxt], False) if nxt is not None else ([], True)
        last = blk.insns[-1]
        fc = last.flow_control
        cls = self.classes(fn)[start][-1]
        succ = []
        if cls[0] == "br":
            succ.append(cls[1])
        elif cls[0] == "goto":
            return [cls[1]], False
        elif cls[0] == "switch":
            return list(dict.fromkeys(cls[1])), False
        elif cls[0] in ("ret", "tail"):
            return [], False
        elif cls[0] == "trap":
            return [], False             # xv_trap never returns
        if fc in (FlowControl.NEXT, FlowControl.CALL, FlowControl.INDIRECT_CALL) and blk.end in fn.blocks:
            return succ + [blk.end], False
        if fc == FlowControl.NEXT and blk.end not in fn.blocks:
            return succ, False            # xv_trap(...); return;  (fell off the end)
        if fc == FlowControl.CONDITIONAL_BRANCH and blk.end in fn.blocks:
            return succ + [blk.end], False
        # anything else continues into the textually next block (or the trailing return)
        nxt = order[i + 1] if i + 1 < len(order) else None
        return (succ + [nxt], False) if nxt is not None else (succ, True)

    def analyze(self, fn, mode: str):
        """mode 'proven' / 'assumed' (summaries) or 'plan'. Returns (summary, plan_or_None, reason)."""
        em = self.em
        COND = _COND()
        want_plan = mode == "plan"
        order = sorted(fn.blocks)
        index = {s: i for i, s in enumerate(order)}
        cls = self.classes(fn)
        exits = set(); unknown_exit = False; fail = None
        state_in: Dict[int, State] = {}
        final_in: Optional[State] = None
        used = set(); fsw_used = False; nx87 = 0; nsync = 0
        work = [fn.entry] if fn.entry in fn.blocks else []
        if work:
            state_in[fn.entry] = State(0, 0, frozenset(), False)
        pending = set(work)
        seen = set()

        def join(target, s):
            nonlocal fail, final_in
            if target is None:
                if final_in is None:
                    final_in = s
                elif final_in.d != s.d:
                    fail = "join-depth"
                else:
                    final_in = State(final_in.d, final_in.mem if final_in.mem == s.mem else None,
                                     final_in.dirty | s.dirty, final_in.fswd or s.fswd)
                return
            old = state_in.get(target)
            if old is None:
                state_in[target] = s
                pending.add(target); work.append(target)
                return
            if old.d != s.d:
                fail = f"join-depth@{target:X}"
                return
            new = State(old.d, old.mem if old.mem == s.mem else None, old.dirty | s.dirty, old.fswd or s.fswd)
            if new.key() != old.key():
                state_in[target] = new
                if target not in pending:
                    pending.add(target); work.append(target)

        def step(start, idx, ins, c, s):
            """Transfer one instruction. Returns (state or None when the rest is unreached, error)."""
            nonlocal nx87, used, fsw_used, nsync, unknown_exit
            k = c[0]
            if k == "x87":
                ctx = Ctx(s.d)
                try:
                    lower_x87_regs(em, ins, c[1], ctx, COND)
                except Unsupported as e:
                    return None, f"insn:{e}"
                nx87 += 1
                used |= ctx.used
                fsw_used = fsw_used or ctx.fsw_read or ctx.fsw_written
                return State(ctx.d, s.mem, s.dirty | frozenset(ctx.written), s.fswd or ctx.fsw_written), None
            if k == "call":
                dl, guarded = self.call_effect(fn, start, idx, c[1], s.d, mode)
                nsync += 1
                if dl == BOT:
                    return None, ("indirect-call" if c[1][0] == "ind" else
                                  f"callee-effect:{c[1][1]:X}" if c[1][0] == "fn" else "callee-effect")
                if dl == TOP:
                    return None, None
                if guarded and want_plan and not self.use_guards:
                    return None, "unproven-call"
                return State(s.d + dl, s.d + dl, frozenset(), False), None
            if k == "ret":
                exits.add(s.d)
            elif k in ("tail", "ctail"):
                nsync += 1
                t = self.table_delta(c[1])
                if t is None and c[1][0] == "fn":
                    t = self.proven.get(c[1][1], TOP) if mode == "proven" else (
                        self.proven.get(c[1][1]) if isinstance(self.proven.get(c[1][1]), int) else self.assumed.get(c[1][1], TOP))
                if t is None or t == BOT:
                    unknown_exit = True
                elif t != TOP:
                    exits.add(s.d + t)
            return s, None

        steps = 0
        while work and fail is None:
            start = work.pop()
            pending.discard(start)
            steps += 1
            if steps > 200000:
                fail = "no-convergence"; break
            s = state_in[start]
            blk = fn.blocks[start]
            for idx, ins in enumerate(blk.insns):
                seen.add((start, idx))
                s, err = step(start, idx, ins, cls[start][idx], s)
                if err is not None:
                    fail = err
                if s is None:
                    break
            if fail is not None or s is None:
                continue
            succ, final = self.block_exit(fn, order, index[start])
            for t in succ:
                if t not in fn.blocks:
                    fail = "succ-outside"; break
                join(t, s)
            if final:
                join(None, s)
        if fail is None and final_in is not None:
            exits.add(final_in.d)
        if fail is not None or unknown_exit:
            summary = BOT
        elif not exits:
            summary = TOP
        elif len(exits) > 1:
            summary = BOT
        else:
            summary = next(iter(exits))
        if not want_plan:
            return summary, None, fail
        if fail is None:
            if nx87 == 0:
                fail = "no-x87"
            elif any((st, i) not in seen for st in fn.blocks for i in range(len(fn.blocks[st].insns))):
                fail = "unreached"
            elif used and max(used) - min(used) > 7 and fn.entry not in self.physical_slots:
                fail = "slot-range"
        if fail is not None:
            return summary, None, fail
        # record the fixed-point state before every instruction and the guarded calls
        plan = Plan(fn)
        plan.physical = fn.entry in self.physical_slots
        for start in order:
            s = state_in.get(start)
            if s is None:
                return summary, None, "unreached"
            for idx, ins in enumerate(fn.blocks[start].insns):
                c = cls[start][idx]
                plan.states[(start, idx)] = s
                if c[0] == "call":
                    dl, guarded = self.call_effect(fn, start, idx, c[1], s.d, mode)
                    if guarded:
                        plan.guards[(start, idx)] = (ins.ip, s.d + dl)
                s, _ = step(start, idx, ins, c, s)
        plan.final_return = final_in
        if used:
            plan.lo, plan.hi = min(used), max(used)
        plan.fsw = fsw_used
        plan.x87 = sum(1 for lst in cls.values() for c in lst if c[0] == "x87")
        plan.syncs = sum(1 for lst in cls.values() for c in lst if c[0] in ("call", "tail", "ctail", "ret"))
        return summary, plan, None

    # ---- whole program -----------------------------------------------------------------
    def fixpoint(self, table: Dict[int, object], mode: str, callers):
        fns = self.disc.functions
        work = sorted(fns)
        queued = set(work)
        while work:
            e = work.pop()
            queued.discard(e)
            self.rounds += 1
            new, _, _ = self.analyze(fns[e], mode)
            old = table.get(e, TOP)
            if old == TOP:
                merged = new
            elif new == TOP or new == old:
                merged = old
            else:
                merged = BOT
            if merged != old:
                table[e] = merged
                for c in callers.get(e, ()):
                    if c not in queued:
                        queued.add(c); work.append(c)

    def run(self):
        fns = self.disc.functions
        callers: Dict[int, set] = {}
        for f in fns.values():
            for s, lst in self.classes(f).items():
                for c in lst:
                    if c[0] in ("call", "tail", "ctail") and c[1][0] == "fn":
                        callers.setdefault(c[1][1], set()).add(f.entry)
        self.fixpoint(self.proven, "proven", callers)
        self.fixpoint(self.assumed, "assumed", callers)
        for e in sorted(fns):
            f = fns[e]
            if not any(c[0] == "x87" for lst in self.classes(f).values() for c in lst):
                continue
            self.x87_count[e] = sum(1 for lst in self.classes(f).values() for c in lst if c[0] == "x87")
            if self.only is not None and e not in self.only:
                self.reasons[e] = "not-selected"; continue
            if e in self.exclude:
                self.reasons[e] = "excluded"; continue
            hook = self.hook_reason(f)
            if hook:
                self.reasons[e] = hook; continue
            summary, plan, reason = self.analyze(f, "plan")
            if plan is not None and plan.x87 < self.min_density * max(plan.syncs, 1):
                plan, reason = None, "density"     # call-heavy, x87-light: the reloads after calls cost more than they save
            if plan is None:
                self.reasons[e] = reason
            else:
                self.plans[e] = plan

    def fallback(self, entry: int, reason: str):
        self.plans.pop(entry, None)
        self.reasons[entry] = reason

    def hook_reason(self, fn) -> Optional[str]:
        h = self.em.hooks
        entry = h.function_entry(fn.entry)
        if any("goto" in line for line in entry):
            return "hook-entry-goto"
        for blk in fn.blocks.values():
            for ins in blk.insns:
                if h.before_instruction(ins.ip):
                    return "hook-before-instruction"
        return None

    # ---- transform_body check ----------------------------------------------------------
    @staticmethod
    def observer_insertions(raw: str, transformed: str) -> bool:
        """True when transformed only inserts observer lines into raw."""
        a, b = raw.splitlines(), transformed.splitlines()
        for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, a, b, autojunk=False).get_opcodes():
            if op == "equal":
                continue
            if op in ("insert", "replace"):
                if op == "replace" and not all(line in b[j1:j2] for line in a[i1:i2]):
                    return False
                for line in b[j1:j2]:
                    if line in a[i1:i2]:
                        continue
                    if not OBSERVER_LINE.match(line):
                        return False
                continue
            return False
        return True

    def report(self):
        from collections import Counter
        reasons = Counter()
        for r in self.reasons.values():
            reasons[r.split("@")[0].split(":")[0] if not r.startswith("insn:") else r] += 1

        def hist(table):
            return dict(Counter("top" if v == TOP else "bottom" if v == BOT else f"{v:+d}" for v in table.values()).most_common())
        guarded = sum(len(p.guards) for p in self.plans.values())
        return {
            "converted": len(self.plans),
            "x87_functions": len(self.x87_count),
            "fallback": len(self.reasons),
            "fallback_reasons": dict(reasons.most_common()),
            "guarded_calls": guarded,
            "functions_with_guards": sum(1 for p in self.plans.values() if p.guards),
            "proven_summaries": hist(self.proven),
            "assumed_summaries": hist(self.assumed),
            "analysis_steps": self.rounds,
            "converted_functions": {f"0x{e:08X}": {"x87": p.x87, "syncs": p.syncs, "guards": len(p.guards),
                                                   "slots": [p.lo, p.hi], "fsw": p.fsw,
                                                   **({"physical_slots": True} if p.physical else {})}
                                    for e, p in sorted(self.plans.items())},
            "fallback_functions": {f"0x{e:08X}": r for e, r in sorted(self.reasons.items())},
            "proven": {f"0x{e:08X}": (v if isinstance(v, str) else int(v)) for e, v in sorted(self.proven.items()) if v != 0},
            "assumed": {f"0x{e:08X}": (v if isinstance(v, str) else int(v)) for e, v in sorted(self.assumed.items())
                        if v != self.proven.get(e)},
        }


def _COND():
    from recompiler.xita_recomp import COND
    return COND
