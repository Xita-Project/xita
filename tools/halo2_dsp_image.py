"""Read-only XDK5849 DSP descriptor audit; never decrypts or executes the image."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def describe_image(data):
    def words(offset, count):
        if offset < 0 or count > (len(data) - offset) // 4:
            raise ValueError('truncated DSP image')
        return struct.unpack_from('<' + 'I' * count, data, offset)

    if len(data) < 0x818:
        raise ValueError('truncated DSP header')
    code_base, code_words, state_offset, state_words, command, reserved = words(0x800, 6)
    code_end = 0x818 + code_words * 4
    descriptor_offset = code_end + state_words * 4
    if code_base or reserved or command != 3 or not code_words or not state_words or state_offset != code_end:
        raise ValueError('unsupported DSP header layout')
    count, scratch = words(descriptor_offset, 2)
    if scratch > 0xFFFFFFFF - 0xC000:
        raise ValueError('overflowing DSP scratch arena')
    if not count or count > (len(data) - descriptor_offset - 8) // 40:
        raise ValueError('invalid DSP effect count')
    key_offset = descriptor_offset + 8 + count * 32
    if key_offset + count * 8 != len(data):
        raise ValueError('unexpected DSP key table extent')
    effects = []
    for index in range(count):
        code, code_size, state, state_size, y, y_size, temp, temp_size = words(descriptor_offset + 8 + index * 32, 8)
        if any(v & 3 for v in (code, code_size, state, state_size, y, y_size, temp, temp_size)):
            raise ValueError('unaligned DSP effect range')
        if not code_size or code < 0x818 or code + code_size > code_end:
            raise ValueError('invalid DSP code range')
        if not state_size or state < state_offset or state + state_size > descriptor_offset:
            raise ValueError('invalid DSP state range')
        if y + y_size > 0x100000000 or temp < 0xC000 or temp + temp_size > 0xC000 + scratch or temp + temp_size > 0x100000000:
            raise ValueError('invalid DSP Y/scratch range')
        effects.append(dict(index=index, code_offset=code, code_bytes=code_size,
                            state_offset=state, state_bytes=state_size,
                            y_offset=y, y_bytes=y_size, scratch_offset=temp - 0xC000,
                            scratch_bytes=temp_size))
    return dict(scope='read-only metadata; no DSP decode, execution, relocation or upload',
                image_bytes=len(data), image_sha256=hashlib.sha256(data).hexdigest(),
                code_offset=0x818, code_bytes=code_words * 4, state_offset=state_offset,
                state_bytes=state_words * 4, descriptor_offset=descriptor_offset,
                effect_count=count, scratch_bytes=scratch, key_table_offset=key_offset,
                key_table_bytes=count * 8, effects=effects)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xbe', type=Path)
    parser.add_argument('--out', required=True, type=Path, help='private JSON destination')
    args = parser.parse_args()
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    from recompiler.xita_recomp import Image
    from recompiler.core.profile import load_profile
    image = Image(str(args.xbe))
    load_profile('halo2_5849').validate_image(image)
    sections = [section for section in image.secs if section[4] == 'DSPImage']
    if len(sections) != 1:
        raise ValueError('expected one DSPImage section')
    section = sections[0]
    report = describe_image(image.bytes_at(section[0], section[2]))
    report['xbe_sha256'] = hashlib.sha256(image.data).hexdigest()
    report['section_address'] = section[0]
    args.out.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
