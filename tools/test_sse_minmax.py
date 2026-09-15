#!/usr/bin/env python3
"""Synthetic legacy MINPS/MAXPS generation and native x86 result/status oracles."""
from pathlib import Path
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler import xita_recomp as recomp
from tools.test_game_profiles import fixture


def generate(directory):
    original, _ = fixture()
    data = bytearray(0x3000); data[:len(original)] = original
    data[0x1000:] = b'\xCC' * 0x2000
    struct.pack_into('<I', data, 0x408, 0x2000)
    struct.pack_into('<I', data, 0x410, 0x2000)
    entries = []
    for opcode in (0x5D, 0x5F):
        for destination in range(8):
            for source in range(9):
                memory = source == 8
                code = bytes([0x0F, opcode, (destination << 3) | (0 if memory else 0xC0 | source), 0xC3])
                offset = len(entries) * 16
                data[0x1000 + offset:0x1000 + offset + len(code)] = code
                entries.append((0x11000 + offset, destination, source, int(opcode == 0x5F)))
    xbe = directory / 'test.xbe'; xbe.write_bytes(data)
    image = recomp.Image(str(xbe)); discovery = recomp.Discovery(image, {}, {}, lambda *_: None)
    for address, *_ in entries: discovery.add_root(address)
    discovery.run()
    emitter = recomp.Emitter(image, discovery, {}, {}, str(directory), 1); emitter.write_all()
    assert len(discovery.functions) == 144 and not emitter.unimpl
    (directory / 'cases.h').write_text('\n'.join(
        f'{{f_{address:08X}, {dst}, {src}, {maximum}}},' for address, dst, src, maximum in entries))


class PackedMinMax(unittest.TestCase):
    def test_emitted_state_and_native_instruction_oracles(self):
        compiler = shutil.which('cc'); self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory(prefix='xita-minmax-sse-') as path:
            directory = Path(path); generate(directory)
            flags = ['-O2', '-frounding-math', '-ffp-contract=off']
            if os.environ.get('XITA_TEST_SANITIZE'):
                flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
            command = [compiler, '-std=gnu11', *flags, '-I', str(ROOT / 'recomp'),
                       '-I', str(directory), str(ROOT / 'tools/test_sse_minmax.c'),
                       '-lm', '-o', str(directory / 'test')]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(directory / 'test')], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__': unittest.main()
