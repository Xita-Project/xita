#!/usr/bin/env python3
"""Exercise the memory recorder with ordered synthetic ARM-query access receipts.

Receipts contain physical read/write events, page mappings and initial/final
64-byte blocks. They must come from a traced execution independently compared
against an untraced oracle. This tests the recorder, not capture completeness,
CPU/FP replay, scheduling, or Vita frame time. No game data is bundled here.
"""
import argparse
import base64
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
U8, U32 = C.c_ubyte, C.c_uint32


class View(C.Structure):
    _fields_ = [('arena', C.POINTER(U8)), ('usable_size', U32),
                ('pages', C.POINTER(U32)), ('page_count', U32)]


def library(directory):
    bridge = directory / 'bridge.c'
    bridge.write_text('#include "xk_query_memory.h"\n'
                      'unsigned record_size(void){return sizeof(XvQueryMemory);}\n')
    output = directory / 'query-memory.so'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-g',
                    '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-I' + str(ROOT / 'recomp/kernel'), str(bridge),
                    str(ROOT / 'recomp/kernel/xk_query_memory.c'),
                    '-o', str(output)], check=True)
    lib = C.CDLL(str(output))
    lib.record_size.restype = C.c_uint
    for name in ('begin', 'finish', 'validate', 'replay'):
        f = getattr(lib, 'xv_query_memory_' + name)
        f.argtypes = [C.c_void_p, C.POINTER(View)]
        f.restype = C.c_int
    for name in ('read', 'write', 'mapping'):
        f = getattr(lib, 'xv_query_memory_' + name)
        f.argtypes = [C.c_void_p, U32, U32]
        f.restype = C.c_int
    return lib


def check(lib, path):
    receipt = json.loads(path.read_text())
    size = receipt['arena_size']
    assert 0 < size <= 256 << 20 and size % 64 == 0
    before = bytearray(b'\x33' * size)
    final = bytearray(before)
    blocks = set()
    for b in receipt['blocks']:
        off = b['offset']
        assert off % 64 == 0 and 0 <= off <= size - 64 and off not in blocks
        blocks.add(off)
        for name, dst in (('before', before), ('after', final)):
            data = base64.b64decode(b[name], validate=True)
            assert len(data) == 64
            dst[off:off + 64] = data
    pages = (U32 * (1 << 20))()
    mapping = dict(receipt['mappings'])
    assert len(mapping) == len(receipt['mappings'])
    for guest, physical in mapping.items():
        assert 0 <= guest < len(pages) and physical % 4096 == 0
        assert 0 <= physical <= size - 4096
        pages[guest] = physical
    arena = (U8 * size).from_buffer_copy(before)
    view = View(arena, size, pages, len(pages))
    storage = C.create_string_buffer(lib.record_size() + 63)
    state = (C.addressof(storage) + 63) & ~63
    assert lib.xv_query_memory_begin(state, C.byref(view))
    for guest, physical in mapping.items():
        assert lib.xv_query_memory_mapping(state, guest, physical)
    reads, writes = set(), set()
    for write, offset, length in receipt['accesses']:
        assert write in (0, 1) and 0 <= offset <= size and 0 <= length <= size - offset
        touched = set(range(offset, offset + length))
        assert all((a & ~63) in blocks for a in touched)
        if write:
            writes.update(touched)
        else:
            reads.update(touched - writes)
        function = lib.xv_query_memory_write if write else lib.xv_query_memory_read
        assert function(state, offset, length), (path.name, offset, length)
    # First-touch snapshots are taken before a block's first store. Ordered
    # masks suffice here; install the actual final bytes before finish.
    C.memmove(arena, bytes(final), size)
    assert lib.xv_query_memory_finish(state, C.byref(view))
    C.memmove(arena, bytes(before), size)
    frozen = C.string_at(state, lib.record_size())
    assert lib.xv_query_memory_replay(state, C.byref(view))
    assert bytes(arena) == final

    C.memmove(arena, bytes(before), size)
    for a in sorted(reads):
        arena[a] ^= 1
        assert not lib.xv_query_memory_validate(state, C.byref(view))
        arena[a] ^= 1
    # Actual replay rejection must be atomic, including when a late block fails.
    representatives = {a & ~63: a for a in sorted(reads)}
    for a in representatives.values():
        arena[a] ^= 1
        snapshot = bytes(arena)
        assert not lib.xv_query_memory_replay(state, C.byref(view))
        assert bytes(arena) == snapshot
        arena[a] ^= 1
    for guest, physical in mapping.items():
        pages[guest] = physical ^ 4096
        assert not lib.xv_query_memory_replay(state, C.byref(view))
        assert bytes(arena) == before
        pages[guest] = physical

    changed = bytearray(before)
    for block in blocks:
        for a in range(block, block + 64):
            if a not in reads:
                changed[a] ^= 0x5a
    expected = bytearray(changed)
    for a in writes:
        expected[a] = final[a]
    C.memmove(arena, bytes(changed), size)
    assert lib.xv_query_memory_replay(state, C.byref(view))
    assert bytes(arena) == expected
    assert C.string_at(state, lib.record_size()) == frozen
    return dict(name=receipt['name'], events=len(receipt['accesses']),
                read_bytes=len(reads), write_bytes=len(writes),
                blocks=len(blocks), mappings=len(mapping),
                dependency_rejections=len(reads),
                atomic_rejections=len(representatives) + len(mapping),
                record_bytes=lib.record_size())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('receipts', type=Path, nargs='+')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='xita-query-memory-trace-') as d:
        lib = library(Path(d))
        rows = []
        for path in args.receipts:
            row = check(lib, path)
            rows.append(row)
            print('PASS', row, flush=True)
    if args.output:
        args.output.write_text(json.dumps(rows, indent=2) + '\n')


if __name__ == '__main__':
    main()
