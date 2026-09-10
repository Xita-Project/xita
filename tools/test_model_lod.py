#!/usr/bin/env python3
"""Exercise the production LOD override; optional owned maps/XBE stay local.

python3 tools/test_model_lod.py [map ...] [--xbe path/to/default.xbe]
--xbe additionally runs the original Xbox 3925 selector with Unicorn.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler.halo_map import HaloMap, TAG_BASE


def put(b, off, fmt, *values):
    struct.pack_into('<' + fmt, b, off, *values)


def fixture():
    b = bytearray(8192)
    put(b, 0, 'I', TAG_BASE + 0x28)
    put(b, 12, 'I', 3)
    put(b, 0x20, 'I', int.from_bytes(b'tags', 'big'))
    for i, (group, name, data) in enumerate([
        ('mode', 'characters\\test\\test', 0x400),
        ('bipd', 'characters\\test\\test', 0x500),
        ('scen', 'scenery\\test\\test', 0x700),
    ]):
        p = 0x28 + i * 32
        put(b, p, 'III', int.from_bytes(group.encode(), 'big'),
            int.from_bytes(b'obje', 'big') if i else 0, 0)
        put(b, p + 12, 'III', 0xabcd0000 + i, TAG_BASE + 0x100 + i * 80, TAG_BASE + data)
        raw = name.encode() + b'\0'
        b[0x100 + i * 80:0x100 + i * 80 + len(raw)] = raw
    for p in (0x500, 0x700):
        put(b, p + 0x34, 'I', 0xabcd0000)
    put(b, 0x408, '5f', 5, 50, 120, 250, 400)
    put(b, 0x4c4, 'II', 1, TAG_BASE + 0x900)
    put(b, 0x4d0, 'II', 5, TAG_BASE + 0xb00)
    put(b, 0x940, 'II', 1, TAG_BASE + 0xa00)
    put(b, 0xa40, '5H', 0, 1, 2, 3, 4)
    return b


def original_selector(path):
    from recompiler.xbe_parse import XbeParser
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EDX
    raw = Path(path).read_bytes()
    parser = XbeParser(raw, str(path))
    parser.parse()
    start, stop = 0xa27f2, 0xa281b
    off = parser.va_to_offset(start, stop - start)
    assert off is not None
    code = raw[off:off + stop - start]
    assert hashlib.sha256(code).hexdigest() == 'd429edd1a9c47d72b4a05d975d644ae1c4a380477a7a701dd529ae7e6454f952', 'not the supported Xbox selector'
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(0xa2000, 4096)
    uc.mem_write(start, code)
    uc.mem_map(0x100000, 8192)

    def select(cutoffs, size):
        uc.mem_write(0x100008, struct.pack('<5f', *cutoffs))
        uc.mem_write(0x101de8, struct.pack('<f', size))
        uc.reg_write(UC_X86_REG_EBP, 0x100000)
        uc.reg_write(UC_X86_REG_ESP, 0x101000)
        uc.emu_start(start, stop, count=100)
        return uc.reg_read(UC_X86_REG_EDX)
    return select


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('maps', nargs='*')
    ap.add_argument('--xbe')
    args = ap.parse_args()
    select = original_selector(args.xbe) if args.xbe else None
    with tempfile.TemporaryDirectory(prefix='xita-model-lod-') as directory:
        d = Path(directory)
        exe = d / 'quality'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O2',
            '-I' + str(ROOT), '-I' + str(ROOT / 'recomp'), '-ffunction-sections', '-fdata-sections',
            *shlex.split(os.environ.get('QUALITY_TEST_CFLAGS', '')),
            str(ROOT / 'tools/tests/quality_host.c'), str(ROOT / 'recomp/kernel/xk_quality.c'),
            str(ROOT / 'recomp/xv_x86rt.c'), '-Wl,--gc-sections', '-lm', '-o', str(exe)], check=True)

        def run(b, level):
            source, target = d / 'input', d / 'output'
            source.write_bytes(b)
            env = dict(os.environ, XV_MATERIAL_QUALITY='2', XV_GLOW_QUALITY='2',
                XV_PARTICLE_QUALITY='2', XV_DECAL_SECONDS='0', XV_MODEL_DETAIL=str(level))
            subprocess.run([str(exe), str(source), str(target)], env=env, check=True)
            return target.read_bytes()

        b = fixture()
        for level in ('2', '9', '-1', 'invalid'):
            assert run(b, level) == b, ('original/fallback', level)
        for level in (0, 1):
            changed = run(b, level)
            assert changed[:0x40c] == b[:0x40c] and changed[0x41c:] == b[0x41c:]
            expected = (5, 50, 120, 250, 400)
            values = struct.unpack_from('<5f', changed, 0x408)
            assert values[0] == expected[0] and all(a > c for a, c in zip(values[1:], expected[1:]))
            if select:
                # Execute the actual selector across boundaries, not a Python copy of it.
                sizes = sorted({0, 1, 1000} | {v + delta for v in (*expected, *values) for delta in (-.01, 0, .01)})
                old = [select(expected, size) for size in sizes]
                new = [select(values, size) for size in sizes]
                assert all(n <= o for n, o in zip(new, old)) and any(n < o for n, o in zip(new, old))
                assert old[-1] == new[-1] == 4 and old[0] == new[0] == 0

        # Shared weapon/vehicle owners, unusable transitions and damaged pointers
        # must preserve the entire cache; never repair or choose a fallback mesh here.
        guards = [
            ('weapon owner', 0x68, 'I', int.from_bytes(b'weap', 'big')),
            ('vehicle owner', 0x68, 'I', int.from_bytes(b'vehi', 'big')),
            ('external owner', 0x68 + 24, 'I', 1),
            ('truncated owner', 0x68 + 20, 'I', TAG_BASE + len(b) - 10),
            ('bad index', 0x68 + 12, 'I', 0xabcd0008),
            ('NaN cutoff', 0x410, 'f', float('nan')),
            ('negative cutoff', 0x408, 'f', -1),
            ('unordered cutoff', 0x410, 'f', 40),
            ('too many regions', 0x4c4, 'I', 33),
            ('invalid region', 0x4c8, 'I', 0xffffffff),
            ('too many geometries', 0x4d0, 'I', 257),
            ('invalid geometry', 0x4d4, 'I', TAG_BASE + len(b) - 1),
            ('invalid permutation', 0x944, 'I', 0xffffffff),
            ('missing permutation', 0x940, 'I', 0),
            ('missing mesh', 0xa40, 'H', 0xffff),
            ('out of range mesh', 0xa40, 'H', 5),
            ('one mesh', 0xa40, '5H', 0, 0, 0, 0, 0),
            ('no cutoffs', 0x408, '5f', 0, 0, 0, 0, 0),
        ]
        for name, offset, fmt, *values in guards:
            bad = bytearray(b)
            put(bad, offset, fmt, *values)
            assert run(bad, 0) == bad, name
        bad = bytearray(b)
        bad[0x100:0x109] = b'vehicles\\'
        assert run(bad, 0) == bad, 'vehicle model used by scenery'
        print(f'PASS: original defaults, two detail levels, {len(guards)+1} rejection guards' +
            (' and original Xbox selector boundary checks' if select else ''))

        for path in args.maps:
            m = HaloMap(path, str(d / 'maps'))
            original = m.data[m.tag_offset:m.tag_offset + m.tag_size]
            assert run(original, 2) == original, (m.name, 'default changed tags')
            for level in (0, 1):
                changed = run(original, level)
                allowed = set()
                names = []
                for t in m.tags:
                    p = t.data_addr - TAG_BASE
                    if t.groups[0] != 'mode' or t.external or not 0 <= p <= len(original) - 232:
                        continue
                    if changed[p+12:p+28] == original[p+12:p+28]:
                        continue
                    names.append(t.name)
                    assert t.name.startswith(('characters\\', 'scenery\\'))
                    for owner in m.tags:
                        op = owner.data_addr - TAG_BASE
                        if 'obje' in owner.groups and not owner.external and 0 <= op <= len(original)-380:
                            if struct.unpack_from('<I', original, op+0x34)[0] == t.tag_id:
                                assert owner.groups[0] in ('bipd', 'scen') and not owner.name.startswith('cinematics\\')
                    allowed.update(range(p+12, p+28))
                    before = struct.unpack_from('<5f', original, p+8)
                    after = struct.unpack_from('<5f', changed, p+8)
                    assert before[0] == after[0] and all(a >= c for a, c in zip(after, before))
                assert all(i in allowed for i, (a, c) in enumerate(zip(original, changed)) if a != c), (m.name, 'unexpected mutation')
                if m.name in ('bloodgulch', 'a10', 'a30'):
                    assert names, (m.name, 'no authored LODs affected')
                print(m.name, 'detail', level, ':', len(names), 'models adjusted:', ', '.join(names))


if __name__ == '__main__':
    main()
