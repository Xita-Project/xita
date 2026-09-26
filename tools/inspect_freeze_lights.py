#!/usr/bin/env python3
"""Inspect developer freeze-lights.bin; input may contain a torn observation.

Only --head values supplied from a known active cluster are diagnosed. Capturing
512 head slots does not establish that all are active on the current map.
"""
import argparse
import json
import struct
from pathlib import Path

SAMPLE_BYTES = 4 + 16 + 56 + 512 * 4 + 2048 * 12
DUMP_BYTES = 16 + 2 * SAMPLE_BYTES


def inspect(data, heads):
    if len(data) != DUMP_BYTES:
        raise ValueError(f"expected {DUMP_BYTES} bytes, got {len(data)} (possibly partial write)")
    magic, version, size, equal = struct.unpack_from('<4I', data)
    if (magic, version, size) != (0x31464C58, 1, DUMP_BYTES):
        raise ValueError('unsupported capture header')
    actual_equal = data[16:16+SAMPLE_BYTES] == data[16+SAMPLE_BYTES:]
    if equal not in (0, 1) or bool(equal) != actual_equal:
        raise ValueError('inconsistent equal-samples marker')
    result = dict(equal_samples=actual_equal,
                  caveat='Observations are not atomic. Equal samples do not prove consistency.',
                  samples=[])
    for sample in range(2):
        offset = 16 + sample * SAMPLE_BYTES
        valid, *globals_ = struct.unpack_from('<5I', data, offset)
        capacity, stride = struct.unpack_from('<HH', data, offset+20+0x20)
        base, = struct.unpack_from('<I', data, offset+20+0x34)
        entry = dict(valid_mask=valid, globals=[hex(v) for v in globals_],
                     capacity=capacity, stride=stride, records_address=hex(base), chains=[])
        for cluster in heads:
            if not 0 <= cluster < 512:
                raise ValueError('head index must be 0..511')
            chain = dict(cluster=cluster, indices=[], status='incomplete_capture')
            if valid == 15 and 0 < capacity <= 2048 and stride == 12:
                handle, = struct.unpack_from('<I', data, offset+76+cluster*4)
                seen = set()
                while handle != 0xffffffff:
                    index = handle & 0xffff
                    if index >= capacity:
                        chain.update(status='out_of_range', handle=hex(handle))
                        break
                    if index in seen:
                        chain.update(status='cycle_in_capture', handle=hex(handle))
                        break
                    seen.add(index)
                    chain['indices'].append(index)
                    handle, = struct.unpack_from('<I', data, offset+76+2048+index*12+8)
                else:
                    chain['status'] = 'terminates'
            entry['chains'].append(chain)
        result['samples'].append(entry)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--head', type=lambda s: int(s, 0), action='append', default=[])
    args = parser.parse_args()
    try:
        print(json.dumps(inspect(args.capture.read_bytes(), args.head), indent=2))
    except (OSError, ValueError) as exc:
        parser.exit(1, f'capture error: {exc}\n')


if __name__ == '__main__':
    main()
