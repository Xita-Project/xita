#!/usr/bin/env python3
"""Check Vita-compiled draw scans in Cortex-A9 emulation, not an FPS benchmark.

Requires VitaSDK, unicorn and pyelftools. No game data is needed. Each index
routine is checked against a Python oracle, with exact read/write bounds;
constant updates use the actual setter extracted from xv_d3d.c.
Firmware sceClibMemcpy is modeled as an exact byte copy. Instruction counts
exclude that firmware implementation; imported calls and bytes are reported.
"""
from pathlib import Path
import argparse
import hashlib
import json
import random
import struct
import subprocess
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_CODE, UC_MEM_READ, UC_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC,
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_CPU_ARM_CORTEX_A9)

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output-dir', required=True, type=Path)
args = parser.parse_args()
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (root / 'runtime/xv_d3d.c').read_text()
start = source.index('void xv_d3d_SetAllConstants(')
end = source.index('\nvoid ', start + 1)
setter = source[start:end]
wrapper = out / 'draw-scan.c'
wrapper.write_text('''#include "runtime/xv_index_copy.h"
#include "runtime/xv_snapshot_copy.h"
#include "runtime/xv_bytes_equal.h"
static struct { float vsc[192][4]; unsigned vsc_gen; } S;
static unsigned scan_constant_checks, scan_constant_reused;
static uint64_t scan_constant_bytes;
static int enabled;
static int draw_scan_neon(void) { return enabled; }
''' + setter + '''
unsigned test_scalar(void *dst, const void *src, unsigned n) {
    return xv_index_copy_bounds(dst, src, n);
}
unsigned test_neon(void *dst, const void *src, unsigned n) {
    return xv_index_copy_bounds_neon(dst, src, n);
}
unsigned test_snapshot(void *dst, const void *src, unsigned n, void *mirror) {
    xv_snapshot_copy(mirror, dst, src, n); return 1;
}
unsigned test_constants(void *dst, const void *src, unsigned generation, int mode) {
    memcpy(S.vsc, dst, sizeof S.vsc); S.vsc_gen = generation; enabled = mode;
    xv_d3d_SetAllConstants(src);
    memcpy(dst, S.vsc, sizeof S.vsc); return S.vsc_gen;
}
/* Unicorn implements this firmware import at entry, preserving its ABI. */
void *sceClibMemcpy(void *dst, const void *src, unsigned n) { return dst; }
''')
binary = out / 'draw-scan.elf'
subprocess.run(['arm-vita-eabi-gcc', '-O2', '-mthumb', '-mcpu=cortex-a9',
    '-mfpu=neon', '-nostdlib', '-I', str(root),
    str(wrapper), '-Wl,-Ttext=0x10000,-e,test_neon', '-lc', '-lgcc', '-o', str(binary)], check=True)
uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A9)
uc.reg_write(UC_ARM_REG_C1_C0_2, 15 << 20)
uc.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
elf_ranges = []
with binary.open('rb') as f:
    elf = ELFFile(f)
    functions = {name: elf.get_section_by_name('.symtab').get_symbol_by_name(name)[0]['st_value']
        for name in ['test_scalar', 'test_neon', 'test_constants', 'test_snapshot', 'sceClibMemcpy']}
    pages = set()
    for segment in elf.iter_segments():
        if segment['p_type'] != 'PT_LOAD':
            continue
        lo, size = segment['p_vaddr'], segment['p_memsz']
        for page in range(lo & ~4095, (lo + size + 4095) & ~4095, 4096):
            if page not in pages:
                uc.mem_map(page, 4096)
                pages.add(page)
        uc.mem_write(lo, segment.data())
        elf_ranges.append((lo, lo + size))

A, B, C, STACK, END, CAP = 0x200000, 0x400000, 0xa00000, 0x700000, 0x900000, 0x100000
for address, size in [(A, CAP), (B, CAP), (C, CAP), (STACK, 65536), (END, 4096)]:
    uc.mem_map(address, size)
allowed_read, allowed_write, faults = [], [], []
instructions = calls = 0
import_calls = import_bytes = 0

def access(emu, kind, address, size, value, user):
    ranges = allowed_read if kind == UC_MEM_READ else allowed_write
    if any(lo <= address and address + size <= hi for lo, hi in
           ranges + elf_ranges + [(STACK, STACK + 65536)]):
        return
    faults.append((kind, hex(address), size, ranges.copy()))
    emu.emu_stop()

uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, access)

def firmware_copy(emu, address, size, user):
    global import_calls, import_bytes
    dst, src, n = (emu.reg_read(reg) for reg in [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2])
    if n:
        access(emu, UC_MEM_READ, src, n, 0, None)
        access(emu, UC_MEM_WRITE, dst, n, 0, None)
        assert not faults, faults
        emu.mem_write(dst, bytes(emu.mem_read(src, n)))
    import_calls += 1
    import_bytes += n

address = functions['sceClibMemcpy'] & ~1
uc.hook_add(UC_HOOK_CODE, firmware_copy, begin=address, end=address)

def invoke(name, dst, src, n, mode=0):
    global allowed_read, allowed_write, calls
    size = 3072 if name == 'test_constants' else n if name == 'test_snapshot' else n * 2
    allowed_read = [(src, src + size)]
    if name == 'test_constants':
        allowed_read.append((dst, dst + size))
    allowed_write = [(dst, dst + size)]
    if name == 'test_snapshot':
        allowed_write.append((mode, mode + size))
    for register, value in [(UC_ARM_REG_R0, dst), (UC_ARM_REG_R1, src),
            (UC_ARM_REG_R2, n), (UC_ARM_REG_R3, mode),
            (UC_ARM_REG_SP, STACK + 65024), (UC_ARM_REG_LR, END | 1)]:
        uc.reg_write(register, value)
    uc.emu_start(functions[name], END, count=5000000)
    assert not faults, faults
    assert uc.reg_read(UC_ARM_REG_PC) == END
    calls += 1
    return uc.reg_read(UC_ARM_REG_R0)

rng = random.Random(20260908)
index_cases = constant_cases = 0

def index_case(values, a, b):
    global index_cases
    data = struct.pack('<' + 'H' * len(values), *values)
    uc.mem_write(a, data)
    expected = max(values) + 1 if values else 0
    for name in ['test_scalar', 'test_neon']:
        uc.mem_write(b, b'\xa5' * len(data))
        assert invoke(name, b, a, len(values)) == expected, (name, len(values))
        assert bytes(uc.mem_read(b, len(data))) == data
    index_cases += 1

counts = [0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65,
    127, 128, 129, 255, 256, 257, 511, 512, 513]
for n in counts:
    for offset_a in range(16):
        for offset_b in [0, 1, 7, 15]:
            index_case([rng.randrange(65536) for _ in range(n)],
                       A + 4096 + offset_a, B + 4096 + offset_b)
# A lone maximum in each lane and tail exposes incomplete vector reductions.
for n in [7, 8, 9, 15, 16, 17, 31, 32, 33, 65, 257]:
    for position in range(n):
        values = [0] * n
        values[position] = 65535
        index_case(values, A + 1, B + 3)
for n in [1023, 1024, 1025, 32768, 65536, 131072]:
    for values in [[0] * n, [65535] * n, [rng.randrange(65536) for _ in range(n)]]:
        index_case(values, A + 3, B + 15)
# An unmapped page immediately follows each span, in addition to access hooks.
for n in [1, 7, 8, 9, 15, 16, 17, 255, 256, 257, 4096]:
    index_case([rng.randrange(65536) for _ in range(n)], A + CAP - n * 2, B + CAP - n * 2)
assert invoke('test_neon', 0, 0, 0) == 0

for mode in [0, 1]:
    for changed_byte in [None] + list(range(3072)):
        data = bytearray(rng.randbytes(3072))
        uc.mem_write(A, bytes(data))
        if changed_byte is not None:
            data[changed_byte] ^= 0x80
        uc.mem_write(B, bytes(data))
        generation = 0xffffffff if changed_byte == 3071 else 42
        result = invoke('test_constants', A, B, generation, mode)
        assert result == (generation + (changed_byte is not None)) & 0xffffffff
        assert bytes(uc.mem_read(A, 3072)) == data
        constant_cases += 1

snapshot_cases = 0
def snapshot_case(n, a, b, c):
    global snapshot_cases
    data = rng.randbytes(n)
    uc.mem_write(a, data)
    uc.mem_write(b, b'\xa5' * n)
    uc.mem_write(c, b'\x5a' * n)
    assert invoke('test_snapshot', b, a, n, c) == 1
    assert bytes(uc.mem_read(a, n)) == data
    assert bytes(uc.mem_read(b, n)) == data
    assert bytes(uc.mem_read(c, n)) == data
    snapshot_cases += 1

for n in [0, 1, 2, 3, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65,
          127, 128, 129, 255, 256, 257, 511, 512, 513, 4096, 65536]:
    for offset in range(16):
        for dest_offset in [0, 1, 7, 15]:
            snapshot_case(n, A + 4096 + offset, B + 4096 + dest_offset,
                          C + 4096 + ((offset + dest_offset) % 16))
for n in [1, 15, 16, 17, 63, 64, 65, 255, 256, 257, 4096, 65536]:
    snapshot_case(n, A + CAP - n, B + CAP - n, C + CAP - n)
assert invoke('test_snapshot', 0, 0, 0, 0) == 1

def count(emu, address, size, user):
    global instructions
    instructions += 1

hook = uc.hook_add(UC_HOOK_CODE, count)
# Flush translated blocks before counting; timings and cycles are not modeled.
for lo, hi in elf_ranges:
    uc.ctl_remove_cache(lo, hi)
rows = []
for n in [16, 256, 4096]:
    values = [rng.randrange(65536) for _ in range(n)]
    uc.mem_write(A, struct.pack('<' + 'H' * n, *values))
    row = {'indices': n}
    for name in ['test_scalar', 'test_neon']:
        instructions = 0
        import_calls = import_bytes = 0
        assert invoke(name, B, A, n) == max(values) + 1
        row[name + '_instructions'] = instructions
        row[name + '_firmware_copy_calls'] = import_calls
        row[name + '_firmware_copy_bytes'] = import_bytes
    rows.append(row)
constant_rows = []
for position in [None, 0, 1536, 3071]:
    original = rng.randbytes(3072)
    changed = bytearray(original)
    if position is not None:
        changed[position] ^= 1
    row = {'changed_byte': position}
    for mode in [0, 1]:
        uc.mem_write(A, original)
        uc.mem_write(B, bytes(changed))
        instructions = 0
        import_calls = import_bytes = 0
        invoke('test_constants', A, B, 1, mode)
        row[('scalar' if mode == 0 else 'neon') + '_instructions'] = instructions
    constant_rows.append(row)
uc.hook_del(hook)
report = {'index_cases': index_cases, 'constant_cases': constant_cases,
    'snapshot_cases': snapshot_cases,
    'native_calls': calls, 'bounds_faults': faults,
    'firmware_model': 'sceClibMemcpy exact byte copy; instruction counts exclude firmware body and are not hardware cycles or timings',
    'index_instruction_counts_not_cycles': rows,
    'constant_instruction_counts_not_cycles': constant_rows,
    'elf_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
    'source_sha256': {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
        for name in ['runtime/xv_d3d.c', 'runtime/xv_index_copy.h', 'runtime/xv_bytes_equal.h', 'runtime/xv_snapshot_copy.h']}}
(out / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
