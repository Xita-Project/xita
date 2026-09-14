#!/usr/bin/env python3
"""Prepare private GP monitor/effect words by executing the owned XDK decoder.

The generated asset contains owned game code and must not be distributed.
No key material, original instruction bytes, or game image is stored here.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools.halo2_dsp_image import describe_image

MONITOR = (0x386B38, 0x5CC, 'c2527379062a138b53dbaadcb3f9ac5762409ff0e38d2f320aea47f8a3ffd8e8')
DECODER = (0x383E79, 0x175)


def fnv64(data):
    value = 0xCBF29CE484222325
    for byte in data:
        value = ((value ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return value


def prepare(image):
    from recompiler.core.profile import load_profile
    from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
    from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_EAX, UC_X86_REG_EIP
    load_profile('halo2_5849').validate_image(image)
    monitor = image.bytes_at(MONITOR[0], MONITOR[1])
    if hashlib.sha256(monitor).hexdigest() != MONITOR[2]:
        raise ValueError('owned GP monitor fingerprint mismatch')
    data = bytearray(image.bytes_at(0x57DD00, 28768))
    metadata = describe_image(data)
    code = image.bytes_at(*DECODER)
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    for address, size in ((0x380000, 0x10000), (0x500000, 0x1000), (0x600000, 0x1000),
                          (0x610000, 0x1000), (0x620000, 0x10000), (0x700000, 0x1000)):
        machine.mem_map(address, size)
    machine.mem_write(DECODER[0], code)

    def run(address, arguments):
        machine.mem_write(0x500800, struct.pack('<' + 'I' * (1 + len(arguments)), 0x700000, *arguments))
        machine.reg_write(UC_X86_REG_ESP, 0x500800)
        machine.emu_start(address, 0x700000, count=4_000_000)
        if machine.reg_read(UC_X86_REG_EIP) != 0x700000 or machine.reg_read(UC_X86_REG_ESP) != 0x500804 + len(arguments) * 4:
            raise ValueError('owned decoder did not complete its exact ABI')
        return machine.reg_read(UC_X86_REG_EAX)

    run(0x383EF2, [0x600000])
    machine.mem_write(0x610000, bytes(data[metadata['key_table_offset']:]))
    if run(0x383F1F, [0x600000, 0x610000, metadata['key_table_bytes'], 0x610000, 0]):
        raise ValueError('owned key-table decode failed')
    start = metadata['code_offset']
    machine.mem_write(0x620000, bytes(data[start:start + metadata['code_bytes']]))
    cursor = 0
    for effect in metadata['effects']:
        if cursor != effect['code_offset'] - start:
            raise ValueError('noncontiguous owned effect code')
        if run(0x383F1F, [0x610000 + effect['index'] * 8, 0x620000 + cursor,
                         effect['code_bytes'], 0x620000 + cursor, 0]):
            raise ValueError('owned effect code decode failed')
        decoded = bytes(machine.mem_read(0x620000 + cursor, effect['code_bytes']))
        if any(word[0] > 0xFFFFFF for word in struct.iter_unpack('<I', decoded)):
            raise ValueError('decoded effect contains non-24-bit words')
        data[start + cursor:start + cursor + len(decoded)] = decoded
        cursor += len(decoded)
    if metadata['code_bytes'] - cursor != 4:
        raise ValueError('unexpected owned program trailer extent')
    payload = struct.pack('<8sII', b'H2DSP001', len(monitor), len(data)) + monitor + data
    report = dict(scope='owned GP monitor and original-routine-decoded effect image; no API success or DSP execution',
                  xbe_sha256=hashlib.sha256(image.data).hexdigest(),
                  monitor_address=MONITOR[0], monitor_bytes=len(monitor), monitor_sha256=MONITOR[2],
                  decoder_address=DECODER[0], decoder_bytes=len(code), decoder_sha256=hashlib.sha256(code).hexdigest(),
                  image_bytes=len(data), image_sha256=hashlib.sha256(data).hexdigest(),
                  monitor_fnv64=f'{fnv64(monitor):016x}', image_fnv64=f'{fnv64(data):016x}',
                  payload_sha256=hashlib.sha256(payload).hexdigest(), effect_count=metadata['effect_count'])
    return payload, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xbe', type=Path)
    parser.add_argument('--out', type=Path, required=True, help='new private asset destination')
    args = parser.parse_args()
    if args.out.exists() or args.out.with_suffix('.json').exists():
        parser.error('asset/report destination exists; choose a new private path')
    from recompiler.xita_recomp import Image
    payload, report = prepare(Image(str(args.xbe)))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('xb') as target:
        target.write(payload)
    with args.out.with_suffix('.json').open('x') as target:
        json.dump(report, target, indent=2); target.write('\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
