#!/usr/bin/env python3
"""Every thread of a Vita core dump with PC, LR and a return-address scan of its stack, symbolized against the
unstripped ELF (ASLR base taken from the core's module list). Needs github.com/xyzz/vita-parse-core (python 3
patched c_str) and pyelftools 0.29 in a venv; see handoff §42.
  vita_core_threads.py <core.psp2dmp> <xita.elf> [--parser DIR] [--addr2line PATH] [--words 256]"""
import argparse, subprocess, sys, struct
def main():
    ap = argparse.ArgumentParser(); ap.add_argument('core'); ap.add_argument('elf')
    ap.add_argument('--parser', default='/home/birchwoodgod/github/third_party/vita-parse-core'); ap.add_argument('--addr2line', default='/home/birchwoodgod/vitasdk/bin/arm-vita-eabi-addr2line')
    ap.add_argument('--words', type=int, default=256); a = ap.parse_args()
    sys.path.insert(0, a.parser); from core import CoreParser
    core = CoreParser(a.core)
    main = next((m for m in core.modules if m.name.startswith('xita')), core.modules[0])
    base = min(s.start for s in main.segments); print(f'main module {main.name[:16]!r} base {base:#x} ({len(core.threads)} threads)')
    LINK = 0x81000000
    def sym(addr):
        if not (base <= addr < base + 0x2000000): return None
        out = subprocess.run([a.addr2line, '-f', '-e', a.elf, hex(addr - base + LINK)], capture_output=True, text=True).stdout.split('\n')[0]
        return None if out.startswith('??') else out
    def mem(addr, n):
        for s in core.segments:
            if s.vaddr <= addr < s.vaddr + len(s.data):
                off = addr - s.vaddr; return s.data[off:off + n]
        return b''
    for t in core.threads:
        r = getattr(t, 'regs', None)
        pc, lr, sp = (r.gpr[15], r.gpr[14], r.gpr[13]) if r else (t.pc, 0, 0)
        print(f'\n{t.name[:24]:24s} status {t.status:#x} stop {t.stop_reason:#x}  PC {sym(pc) or hex(pc)}  LR {sym(lr) or hex(lr)}')
        if not sp: continue
        data = mem(sp, a.words * 4); seen = []
        for i in range(0, len(data) - 3, 4):
            w = struct.unpack_from('<I', data, i)[0]
            s = sym(w)
            if s and s not in seen[-1:]: seen.append(s)
        if seen: print('   stack:', ' <- '.join(seen[:14]))
if __name__ == '__main__': main()
