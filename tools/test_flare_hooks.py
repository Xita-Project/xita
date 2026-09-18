#!/usr/bin/env python3
"""Check Halo flare guard generation and volatile-output liveness from the XBE.

Requires iced_x86. No generated source is rewritten by this test. --source points
at the preserved build whose HLE stub supplies the one transparent direct call.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as r
from games.halo_ce_3925.hooks import HaloHooks
from games.halo_ce_3925.discovery import HaloDiscovery
from recompiler.halo_flare_hooks import ENTRY, ENTRY_HOOK, BARRIERS, barrier_line, matches_image
from apply_flare_hooks import add_hooks
from iced_x86 import InstructionInfoFactory, Register, OpAccess, FlowControl, RflagsBits


def liveness(img, source):
    # This HLE is called by render-begin before the guest consumes EDX/flags.
    stub = (source/'xv_stubs_default.c').read_text()
    expected = ('void xv_hle_D3DDevice_SetRenderState_Dxt1NoiseEnable(xctx *c) { '
                'xv_trace_call(c, "D3DDevice_SetRenderState_Dxt1NoiseEnable", 1); '
                'c->r[0] = 0; X_RET(1); }')
    assert expected in stub, 'Re-audit the formerly transparent HLE stub'
    factory = InstructionInfoFactory()
    reads = {OpAccess.READ, OpAccess.COND_READ, OpAccess.READ_WRITE, OpAccess.READ_COND_WRITE}
    aliases = [{Register.EAX, Register.AX, Register.AL, Register.AH},
               {Register.ECX, Register.CX, Register.CL, Register.CH},
               {Register.EDX, Register.DX, Register.DL, Register.DH}]
    full = [Register.EAX, Register.ECX, Register.EDX]
    all_flags = (RflagsBits.OF | RflagsBits.SF | RflagsBits.ZF |
                 RflagsBits.AF | RflagsBits.CF | RflagsBits.PF)
    for parent in (0x5D288, 0xBC624, 0x5DC0C):
        todo = [(0x8022A, (parent,), 7, all_flags)]
        seen, kills = set(), set()
        while todo:
            pc, stack, taint, flags = todo.pop()
            state = (pc, stack, taint, flags)
            if state in seen:
                continue
            seen.add(state)
            assert len(seen) < 5000, 'Unbounded proof path'
            ins = next(iter(r.Decoder(32, img.bytes_at(pc, 15), ip=pc)))
            assert not (ins.rflags_read & flags), ('live arithmetic flags', hex(pc), str(ins))
            flags &= ~ins.rflags_modified
            regs = factory.info(ins).used_registers()
            for n, group in enumerate(aliases):
                if not (taint & (1 << n)):
                    continue
                assert not any(u.register in group and u.access in reads for u in regs), ('live register', n, hex(pc), str(ins))
                if any(u.register == full[n] and u.access == OpAccess.WRITE for u in regs):
                    taint &= ~(1 << n)
            if not taint and not flags:
                kills.add(pc)
                continue
            fc = ins.flow_control
            def push(addr, returns=stack):
                todo.append((addr, returns, taint, flags))
            if fc == FlowControl.RETURN:
                assert stack, ('live output escapes caller', hex(pc))
                push(stack[-1], stack[:-1])
            elif fc == FlowControl.CALL:
                target = ins.near_branch_target
                if target == 0x1823C0:
                    taint &= ~1  # HLE returns EAX=0, leaves EDX/flags untouched.
                    push(ins.next_ip)
                else:
                    push(target, stack + (ins.next_ip,))
            elif fc == FlowControl.UNCONDITIONAL_BRANCH:
                push(ins.near_branch_target)
            elif fc == FlowControl.CONDITIONAL_BRANCH:
                push(ins.next_ip)
                # Native hook accepts this parent only with signed view count>0.
                # At this exact image instruction, JLE skips the view loop.
                if parent == 0x5DC0C and pc == 0x5DC16:
                    assert str(ins).startswith('jle '), str(ins)
                else:
                    push(ins.near_branch_target)
            elif fc == FlowControl.NEXT:
                push(ins.next_ip)
            else:
                raise AssertionError(('unproved branch', hex(pc), str(ins)))
        print(f'PASS: caller {parent:08X}: {len(seen)} instruction states; all EAX/ECX/EDX/arithmetic flags dead before use; {len(kills)} terminal definitions')


def hooks(img):
    assert matches_image(img)
    disc = HaloDiscovery(img, {}, img.kernel_imports(), lambda *args: None)
    for addr in (ENTRY, *BARRIERS):
        disc.add_root(addr)
        disc.lift_function(disc.functions[addr])
        disc.split_blocks(disc.functions[addr])
    emitter = r.Emitter(img, disc, {}, img.kernel_imports(), 'unused', 1, hooks=HaloHooks(img))
    for trace in (False, True):
        emitter.trace_funcs = trace
        for addr in (ENTRY, *BARRIERS):
            emitter.hooks.flare_enabled = False
            plain = emitter.emit_function(disc.functions[addr])
            emitter.hooks.flare_enabled = True
            guarded = emitter.emit_function(disc.functions[addr])
            assert guarded == add_hooks(plain), ('emitter/updater mismatch', hex(addr), trace)
            assert add_hooks(guarded) == guarded
    old = img.data
    img.data = bytes([old[0] ^ 1]) + old[1:]
    try:
        assert not r.Emitter(img, disc, {}, img.kernel_imports(), 'unused', 1, hooks=HaloHooks(img)).hooks.flare_enabled
    finally:
        img.data = old
    with tempfile.TemporaryDirectory(prefix='xita-flare-incomplete-') as directory:
        path = Path(directory)/'code_000.c'
        emitter.hooks.flare_enabled = False
        plain = emitter.emit_function(disc.functions[ENTRY])
        path.write_text(plain)
        p = subprocess.run([sys.executable, str(ROOT/'tools/apply_flare_hooks.py'), directory], capture_output=True, text=True)
        assert p.returncode and 'Incomplete generated snapshot' in p.stderr
        assert path.read_text() == plain, 'Incomplete snapshot was partially edited'
    print('PASS: all 8 sites, traced/untraced generation equals selective updater; idempotence, changed-image rejection and incomplete-snapshot nonmutation')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=ROOT/'recomp')
    args = p.parse_args()
    img = r.Image(str(ROOT/'haloce/default.xbe'), str(ROOT/'local/halo_ce_3925/game_manifest.json'))
    hooks(img)
    liveness(img, args.source)
