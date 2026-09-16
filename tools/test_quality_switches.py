#!/usr/bin/env python3
"""Exercise the production visual switches; optional arguments are owned .maps."""
from pathlib import Path
import os
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler.halo_map import HaloMap, TAG_BASE

KEYS = ('XV_TEMP_DECALS', 'XV_COSMETIC_EFFECTS', 'XV_REFLECTIONS', 'XV_OBJECT_SHADOWS')


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def put(data, offset, value):
    struct.pack_into('<I', data, offset, value)


def group(name):
    return int.from_bytes(name.encode(), 'big')


def expected(data, switches):
    """Independent tag-level reference; retained event records must match exactly."""
    out = bytearray(data)
    array = u32(data, 0) - TAG_BASE
    count = u32(data, 12)
    entries = {}
    for i in range(count):
        e = array + i * 32
        entries[u32(data, e + 12)] = e

    def resolve(tag, klass, size):
        e = entries.get(tag)
        if e is None or u32(data, e) != group(klass) or u32(data, e + 24):
            return None
        p = u32(data, e + 20) - TAG_BASE
        return (e, p) if 0 <= p <= len(data) - size else None

    def remove(tag, particle):
        resolved = resolve(tag, 'part' if particle else 'deca', 356 if particle else 268)
        if resolved is None:
            return False
        e, p = resolved
        if not particle:
            flags, kind = struct.unpack_from('<HH', data, p)
            lo, hi = struct.unpack_from('<ff', data, p + 0x78)
            return kind != 3 and not flags & 16 and 0 < lo <= hi <= 86400
        start = u32(data, e + 16) - TAG_BASE
        if not 0 <= start < len(data):
            return False
        name = data[start:start + 256].split(b'\0', 1)
        if len(name) != 2:
            return False
        name = name[0].lower()
        return (all(u32(data, p + f) == 0xffffffff for f in (0x30, 0x54, 0x64))
                and not any(n in name for n in (b'plasma', b'projectile', b'bullet', b'tracer', b'energy'))
                and any(n in name for n in (b'smoke', b'spark', b'dust', b'steam')))

    for tag, e in entries.items():
        if u32(data, e + 24):
            continue
        klass = u32(data, e)
        p = u32(data, e + 20) - TAG_BASE
        if not 0 <= p < len(data):
            continue
        if switches[3] == 0 and group('obje') in struct.unpack_from('<III', data, e) and p + 380 <= len(data):
            flags = struct.unpack_from('<H', data, p + 2)[0]
            struct.pack_into('<H', out, p + 2, flags | 1)
        if switches[2] == 0:
            offsets = ((0x330, 0xffffffff), (0x2f4, 0), (0x2f8, 0)) if klass == group('senv') else (
                ((0x170, 0xffffffff), (0x144, 0), (0x154, 0)) if klass == group('soso') else ())
            for offset, value in offsets:
                put(out, p + offset, value)
        if klass != group('effe') or p + 64 > len(data):
            continue
        n, address = struct.unpack_from('<II', data, p + 0x34)
        first = address - TAG_BASE
        if n > 32 or not 0 <= first <= len(data) - n * 68:
            continue
        arrays = []
        for j in range(n):
            for kind, stride, field in ((0, 104, 0x2c), (1, 232, 0x38)):
                header = first + j * 68 + field
                length, address = struct.unpack_from('<II', data, header)
                start = address - TAG_BASE
                arrays.append((kind, stride, header, length, start))
        if any(n > 32 or (n and not 0 <= start <= len(data) - n * stride)
               for _, stride, _, n, start in arrays):
            continue
        for kind, stride, header, n, start in arrays:
            if switches[kind]:
                continue
            retained = []
            for j in range(n):
                record = data[start + j * stride:start + (j + 1) * stride]
                ref = 0x54 if kind else 0x18
                if u32(record, ref) == group('part' if kind else 'deca') and remove(u32(record, ref + 12), kind):
                    continue
                retained.append(record)
            put(out, header, len(retained))
            joined = b''.join(retained)
            out[start:start + len(joined)] = joined
    return bytes(out)


def fixture():
    data = bytearray(65536)
    tags = [('effe', 'effect'), ('deca', 'temporary'), ('deca', 'water'),
            ('deca', 'permanent'), ('deca', 'zero_lifetime'), ('part', 'smoke'),
            ('part', 'plasma_smoke'), ('part', 'spark_with_callback'), ('part', 'steam'),
            ('jpt!', 'damage'), ('snd!', 'sound'), ('weap', 'weapon'),
            ('senv', 'terrain'), ('soso', 'model'), ('effe', 'invalid_effect')]
    put(data, 0, TAG_BASE + 0x40); put(data, 12, len(tags)); put(data, 0x20, group('tags'))
    ids = [0x12340000 + i for i in range(len(tags))]
    for i, (klass, name) in enumerate(tags):
        entry = 0x40 + i * 32
        p = 0x1000 + i * 1024
        struct.pack_into('<8I', data, entry, group(klass), group('obje') if klass == 'weap' else 0,
                         0, ids[i], TAG_BASE + 0x800 + i * 48, TAG_BASE + p, 0, 0)
        data[0x800 + i * 48:0x800 + i * 48 + len(name) + 1] = name.encode() + b'\0'
        if klass == 'deca':
            struct.pack_into('<HH', data, p, 16 if i == 3 else 0, 3 if i == 2 else 0)
            struct.pack_into('<4f', data, p + 0x78, 0 if i == 4 else 10, 20, 1, 2)
        if klass == 'part':
            for f in (0x30, 0x54, 0x64): put(data, p + f, ids[9] if i == 7 else 0xffffffff)
        if klass in ('senv', 'soso'):
            for f in range(0, 836 if klass == 'senv' else 440, 4): put(data, p + f, 0x3f123456)
    for effect, event, parts, particles in ((0, 0x6000, 0x6100, 0x6800), (14, 0x8000, 0x8100, 0x8800)):
        struct.pack_into('<II', data, 0x1000 + effect * 1024 + 0x34, 1, TAG_BASE + event)
        part_ids = (9, 1, 2, 3, 4, 10, 1)  # damage/sound before and after optional decals
        struct.pack_into('<II', data, event + 0x2c, len(part_ids), TAG_BASE + parts)
        struct.pack_into('<II', data, event + 0x38, 33 if effect else 4, TAG_BASE + particles)
        for j, tag in enumerate(part_ids):
            put(data, parts + j * 104 + 0x18, group(tags[tag][0])); put(data, parts + j * 104 + 0x24, ids[tag])
        for j, tag in enumerate((5, 6, 7, 8)):
            put(data, particles + j * 232 + 0x54, group('part')); put(data, particles + j * 232 + 0x60, ids[tag])
    return bytes(data)


with tempfile.TemporaryDirectory(prefix='xita-switches-') as directory:
    d = Path(directory); exe = d / 'quality'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-O2', '-I'+str(ROOT), '-I'+str(ROOT/'recomp'),
                    '-ffunction-sections', '-fdata-sections', *shlex.split(os.environ.get('QUALITY_TEST_CFLAGS', '')),
                    str(ROOT/'tools/tests/quality_host.c'), str(ROOT/'recomp/kernel/xk_quality.c'),
                    str(ROOT/'recomp/xv_x86rt.c'), '-Wl,--gc-sections', '-lm', '-o', str(exe)], check=True)
    cases = [('synthetic guards', fixture())]
    for path in sys.argv[1:]:
        m = HaloMap(path, str(d/'cache'))
        cases.append((m.name, m.data[m.tag_offset:m.tag_offset+m.tag_size]))
    for name, original in cases:
        source, target = d/'input', d/'output'; source.write_bytes(original)
        base = dict(os.environ, XV_MATERIAL_QUALITY='2', XV_GLOW_QUALITY='2', XV_PARTICLE_QUALITY='2',
                    XV_DECAL_SECONDS='0', XV_MODEL_DETAIL='2')
        for switches in ((1,1,1,1), (0,1,1,1), (1,0,1,1), (1,1,0,1), (1,1,1,0), (0,0,0,0)):
            env = dict(base, **dict(zip(KEYS, map(str, switches))))
            subprocess.run([str(exe), str(source), str(target)], env=env, check=True)
            result = target.read_bytes(); want = expected(original, switches)
            assert result == want, (name, switches, next(i for i, (a,b) in enumerate(zip(result,want)) if a != b))
        for bad in ('-1', '2', 'garbage', '0 trailing', '999999999999999999999999'):
            env = dict(base, **dict.fromkeys(KEYS, bad))
            subprocess.run([str(exe), str(source), str(target)], env=env, check=True)
            assert target.read_bytes() == original, (name, bad)
        print(name, ': defaults/independent switches/combined switches/invalid settings match; protected bytes preserved')
