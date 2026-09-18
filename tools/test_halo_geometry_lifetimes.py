#!/usr/bin/env python3
"""Check audit failure handling and execute owned pool routines with Unicorn x86.

These original routines have no child calls, so no HLE or timing model is used.
Requires an owned 3925 XBE, iced-x86 and unicorn; emits no owned game bytes.
"""
import argparse
import json
from pathlib import Path
import struct
import tempfile

from audit_halo_geometry_lifetimes import collect_calls, decode_span, verify_call, verify_image
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX
from unicorn.x86_const import UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_EIP


def rejected(call):
    try:
        call()
    except ValueError:
        return
    raise AssertionError('invalid audit input was accepted')


def run(img):
    negative = 0
    with tempfile.TemporaryDirectory(prefix='xita-lifetime-') as directory:
        path = Path(directory) / 'code_000.c'
        # The same guest PC occurs in a fast path, a fallback and another lifted
        # entry. It is still one original call site, with two owner aliases.
        path.write_text('void f_0007A630(xctx *c)\n{\n'
                        '/* 0007A69D  call 001858D0h */\n'
                        '/* 0007A69D  call 001858D0h */\n}\n'
                        'void f_0007A63B(xctx *c)\n{\n'
                        '/* 0007A69D  call 001858D0h */\n}\n')
        row = collect_calls(img, [path])['0x1858d0']
        assert row['unique_sites'] == 1
        assert row['sites'][0]['lifted_owners'] == ['0x7a630', '0x7a63b']
        path.write_text('void f_0007A630(xctx *c)\n{\n'
                        '/* 0007A69D  call 00185870h */\n}\n')
        rejected(lambda: collect_calls(img, [path])); negative += 1
        path.write_text('/* 0007A69D  call 001858D0h */\n')
        rejected(lambda: collect_calls(img, [path])); negative += 1
        rejected(lambda: verify_call(img, 0x7a69e, 0x1858d0)); negative += 1
        rejected(lambda: decode_span(img, 0x7a630, 0x7a69e)); negative += 1
        changed = bytearray(img.data)
        changed[-1] ^= 1
        damaged = Path(directory) / 'changed.xbe'
        damaged.write_bytes(changed)
        rejected(lambda: verify_image(damaged)); negative += 1

    def machine():
        u = Uc(UC_ARCH_X86, UC_MODE_32)
        u.mem_map(0x10000, 0x400000)
        for lo, hi in [(0x7a6d0, 0x7a73a), (0x7a740, 0x7a7ad), (0x7a9d0, 0x7aa05)]:
            u.mem_write(lo, img.bytes_at(lo, hi - lo))
        return u

    def put(u, address, value):
        u.mem_write(address, struct.pack('<I', value))

    def get(u, address):
        return struct.unpack('<I', u.mem_read(address, 4))[0]

    def execute(u, entry):
        put(u, 0x3ff000, 0x3f0000)
        u.reg_write(UC_X86_REG_ESP, 0x3ff000)
        u.emu_start(entry, 0x3f0000, count=512)
        assert u.reg_read(UC_X86_REG_EIP) == 0x3f0000
        assert u.reg_read(UC_X86_REG_ESP) == 0x3ff004
        return u.reg_read(UC_X86_REG_EAX)

    vertices = 0
    for fmt in (0, 5, 6, 11):
        for used, count, handle in ((0, 1, 0), (3, 8, 7), (52, 12, 7),
                                     (53, 12, 7), (0, 0, 7), (0, 2, 0x3ff)):
            u = machine()
            pool, record = 0x278ab0 + fmt * 20, 0x278ba0 + handle * 16
            put(u, pool, used); put(u, pool + 4, 64); put(u, 0x27cba0, handle)
            before = bytes([0xa5]) * 12 + struct.pack('<I', 0xdead1234)
            u.mem_write(record, before)
            u.reg_write(UC_X86_REG_EAX, fmt); u.reg_write(UC_X86_REG_ESI, count)
            result = execute(u, 0x7a6d0)
            admitted = count > 0 and used < 64 - count and handle < 0x3ff
            assert result == (handle if admitted else 0xffffffff)
            # Even successful reservation leaves the cached writable pointer.
            assert get(u, record + 12) == 0xdead1234
            assert get(u, pool) == used + (count if admitted else 0)
            assert get(u, 0x27cba0) == handle + admitted
            if admitted:
                assert bytes(u.mem_read(record, 12)) == struct.pack('<H2sII', fmt, b'\xa5\xa5', used, count)
            else:
                assert bytes(u.mem_read(record, 16)) == before
            vertices += 1

    indices = 0
    for used, count, handle in ((0, 1, 0), (17, 8, 7), (0x7fff, 1, 7),
                                 (0x7fff, 2, 7), (0, 0, 7), (0, 2, 0x3ff)):
        u = machine()
        record = 0x27cba8 + handle * 12
        put(u, 0x27fbac, used); put(u, 0x27fba8, handle)
        before = bytes([0x5a]) * 8 + struct.pack('<I', 0xdead5678)
        u.mem_write(record, before)
        u.reg_write(UC_X86_REG_EDX, count)
        result = execute(u, 0x7a740)
        admitted = count > 0 and used < 0x8000 - count and handle < 0x3ff
        assert result == (handle if admitted else 0xffffffff)
        assert get(u, record + 8) == 0xdead5678
        assert get(u, 0x27fbac) == used + (count if admitted else 0)
        assert get(u, 0x27fba8) == handle + admitted
        assert bytes(u.mem_read(record, 8)) == (struct.pack('<II', used, count) if admitted else before[:8])
        indices += 1

    pointers = 0
    for data in (0x100000, 0x80100000, 0xa1234000):
        for first in (0, 1, 200):
            for handle in (0, 100, 0x3fe):
                u = machine()
                record = 0x27cba8 + handle * 12
                put(u, 0x27fbb0, 0x300000); put(u, 0x300004, data)
                put(u, record, first); put(u, record + 8, 0xdead5678)
                u.mem_write(0x27fbb4, b'\x01')
                u.reg_write(UC_X86_REG_ECX, handle)
                result = execute(u, 0x7a9d0)
                assert result == get(u, record + 8) == data + first * 6
                assert u.mem_read(0x27fbb4, 1) == b'\x00'
                pointers += 1
    u = machine(); u.reg_write(UC_X86_REG_ECX, 0xffffffff)
    assert execute(u, 0x7a9d0) == 0
    return dict(result='PASS', rejected_inputs=negative, deduplicated_call_case=1,
                original_vertex_reservations=vertices, original_index_reservations=indices,
                original_direct_index_pointers=pointers + 1,
                scope='Original x86 behavior with synthetic pool data; not write coverage or performance')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--xbe', required=True, type=Path)
    args = p.parse_args()
    print(json.dumps(run(verify_image(args.xbe))))
