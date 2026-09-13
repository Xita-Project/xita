#!/usr/bin/env python3
"""Synthetic on-disk exFAT allocation tests; never accesses a device."""
import contextlib
import base64
import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest

from audit_vita_storage import AuditError, Volume, main


def seal(data, bits, excluded):
    total = 0
    for i, value in enumerate(data):
        if i not in excluded:
            low = total & 1
            total = ((total >> 1) + (low << (bits - 1)) + value) % (1 << bits)
    return total


def entry(name, first, length, directory=False, contiguous=False):
    encoded = name.encode('utf-16-le')
    count = (len(encoded) + 29) // 30
    result = bytearray((2 + count) * 32)
    result[0:2] = bytes([0x85, count + 1])
    struct.pack_into('<H', result, 4, 0x10 if directory else 0x20)
    result[32:36] = bytes([0xc0, 3 if contiguous else 1, 0, len(encoded) // 2])
    struct.pack_into('<Q', result, 40, length)
    struct.pack_into('<I', result, 52, first)
    struct.pack_into('<Q', result, 56, length)
    for i in range(count):
        result[64 + 32*i] = 0xc1
        chunk = encoded[30*i:30*(i+1)]
        result[66 + 32*i:66 + 32*i + len(chunk)] = chunk
    struct.pack_into('<H', result, 2, seal(result, 16, {2, 3}))
    return result


def fixture(fats=2, overlap=False, free=False, cycle=False):
    heap, count, active = 24 + fats, 64, fats - 1
    data = bytearray((heap + count) * 512)
    data[3:11], data[510:512] = b'EXFAT   ', b'\x55\xaa'
    struct.pack_into('<Q', data, 72, len(data) // 512)
    struct.pack_into('<IIIII', data, 80, 24, 1, heap, count, 2)
    struct.pack_into('<HH', data, 104, 0x100, active)
    data[108:111] = bytes([9, 0, fats])
    value = seal(data[:11*512], 32, {106, 107, 112})
    data[11*512:12*512] = struct.pack('<I', value) * 128

    def fat(which, cluster, next_cluster):
        struct.pack_into('<I', data, (24 + which) * 512 + cluster * 4, next_cluster)

    def put(cluster, payload):
        offset = (heap + cluster - 2) * 512
        data[offset:offset + len(payload)] = payload

    for which in range(fats):
        for cluster in (2, 3, 4, 5, 6, 8, 9):
            fat(which, cluster, 0xffffffff)
        fat(which, 7, 9 if which == active else 8)
    if cycle:
        fat(active, 9, 7)
    root = bytearray()
    for which in range(fats):
        bitmap = bytearray(32)
        bitmap[0:2] = bytes([0x81, which])
        struct.pack_into('<IQ', bitmap, 20, 3 + which, 8)
        root += bitmap
        # A stale inactive bitmap is intentionally all zeroes.
        bits = bytearray(b'\xff' * 8 if which == active else b'\x00' * 8)
        if free and which == active:
            bits[(9 - 2) // 8] &= ~(1 << ((9 - 2) % 8))
        put(3 + which, bits)
    root += entry('app', 5, 512, directory=True)
    put(2, root)
    put(5, entry('XITA00001', 6, 512, directory=True, contiguous=True))
    put(6, entry('eboot.bin', 7, 1024) +
        entry('eboot.before.bin', 9 if overlap else 10, 1024, contiguous=True))
    return data


PATHS = ['app/XITA00001/eboot.bin', 'app/XITA00001/eboot.before.bin']


class StorageTests(unittest.TestCase):
    def report(self, data, paths=PATHS):
        return Volume(io.BytesIO(data)).audit(paths)

    def test_active_second_fat_and_bitmap(self):
        result = self.report(fixture())
        self.assertEqual(result['active_fat'], 1)
        self.assertEqual(result['files'][0]['clusters'], [7, 9])
        self.assertEqual(result['files'][1]['clusters'], [10, 11])
        self.assertEqual(result['findings'], [])

    def test_single_fat(self):
        result = self.report(fixture(fats=1))
        self.assertEqual(result['active_fat'], 0)
        self.assertEqual(result['findings'], [])

    def test_overlap_and_active_free_bit(self):
        result = self.report(fixture(overlap=True, free=True))
        self.assertEqual([f['kind'] for f in result['findings']],
                         ['referenced_clusters_marked_free'] * 2 + ['selected_file_cluster_overlap'])
        self.assertEqual(result['findings'][-1]['clusters'], [9])

    def test_contiguous_ignores_fat(self):
        data = fixture()
        struct.pack_into('<I', data, 25 * 512 + 10 * 4, 10)
        self.assertEqual(self.report(data)['files'][1]['clusters'], [10, 11])

    def test_chain_cycle_or_excess_stops(self):
        with self.assertRaisesRegex(AuditError, 'longer|cycle'):
            self.report(fixture(cycle=True))

    def test_chain_too_short(self):
        data = fixture()
        struct.pack_into('<I', data, 25 * 512 + 7 * 4, 0xffffffff)
        with self.assertRaisesRegex(AuditError, 'shorter'):
            self.report(data)

    def test_invalid_cluster_stops(self):
        data = fixture()
        struct.pack_into('<I', data, 25 * 512 + 7 * 4, 0xfffffff7)
        with self.assertRaisesRegex(AuditError, 'out of range'):
            self.report(data)

    def test_main_boot_checksum(self):
        data = fixture()
        data[100] ^= 1
        with self.assertRaisesRegex(AuditError, 'boot-region checksum'):
            self.report(data)

    def test_mutable_boot_flags_excluded(self):
        data = fixture()
        data[106] |= 6
        data[112] = 50
        report = self.report(data)
        self.assertTrue(report['volume_dirty'])
        self.assertTrue(report['media_failure_flag'])

    def test_entry_checksum_stops(self):
        data = fixture()
        data[(26 + 6 - 2) * 512 + 70] ^= 1
        with self.assertRaisesRegex(AuditError, 'entry-set checksum'):
            self.report(data)

    def test_exact_paths_and_duplicates(self):
        for paths in (['App'], ['app/../app'], ['./app'], [PATHS[0], PATHS[0]]):
            with self.subTest(paths=paths), self.assertRaises(AuditError):
                self.report(fixture(), paths)

    def test_offset_and_truncation(self):
        data = fixture()
        result = Volume(io.BytesIO(bytes(4096) + data), 4096).audit(PATHS)
        self.assertFalse(result['findings'])
        with self.assertRaisesRegex(AuditError, 'truncated'):
            self.report(data[:12000])

    def test_cli_is_read_only_and_exit_status(self):
        for overlap in (False, True):
            with tempfile.TemporaryDirectory() as temp:
                source = Path(temp) / 'test.img'
                source.write_bytes(fixture(overlap=overlap))
                before = hashlib.sha256(source.read_bytes()).hexdigest()
                with contextlib.redirect_stdout(io.StringIO()):
                    status = main([str(source), *PATHS])
                self.assertEqual(status, int(overlap))
                self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before)

    def test_zero_root_link_saves_context_and_raw_evidence(self):
        data = fixture()
        struct.pack_into('<I', data, 25 * 512 + 2 * 4, 0)
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'test.img'
            source.write_bytes(data)
            output, errors = io.StringIO(), io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
                status = main([str(source), *PATHS, '--capture-metadata'])
            report = json.loads(output.getvalue())
            self.assertEqual(status, 2)
            self.assertEqual(report['context']['stage'], 'root_directory')
            self.assertEqual(report['context']['last_fat_link']['cluster'], 2)
            self.assertEqual(report['context']['last_fat_link']['value'], 0)
            self.assertEqual([v['value'] for v in report['fat_probes'][0]['fat_values']],
                             [0xffffffff, 0])
            self.assertNotIn('files', report)  # no inactive-FAT fallback result
            self.assertIn('cluster out of range: 0x0', errors.getvalue())
            self.assertEqual(report['unvalidated_first_cluster_preview']['offset'], 26 * 512)
            reads = report['metadata_capture']['reads']
            self.assertGreater(len(reads), 3)
            for read in reads:
                actual = base64.b64decode(read['base64'])
                self.assertEqual(actual, data[read['offset']:read['offset'] + read['size']])
                self.assertEqual(hashlib.sha256(actual).hexdigest(), read['sha256'])
            self.assertEqual(source.read_bytes(), data)

    def test_boot_and_open_errors_also_emit_json(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'test.img'
            for stage in ('open_source', 'boot'):
                if stage == 'boot':
                    source.write_bytes(b'bad')
                output = io.StringIO()
                with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
                    status = main([str(source), *PATHS, '--capture-metadata'])
                report = json.loads(output.getvalue())
                self.assertEqual(status, 2)
                self.assertEqual(report['context']['stage'], stage)

    def test_selected_path_error_reports_allocation_without_raw_capture(self):
        data = fixture()
        struct.pack_into('<I', data, 25 * 512 + 7 * 4, 0)
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / 'test.img'
            source.write_bytes(data)
            output = io.StringIO()
            with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
                status = main([str(source), *PATHS])
            report = json.loads(output.getvalue())
            self.assertEqual(status, 2)
            self.assertEqual(report['context']['path'], PATHS[0])
            self.assertEqual(report['context']['allocation']['first'], 7)
            self.assertNotIn('metadata_capture', report)
            self.assertEqual(source.read_bytes(), data)


if __name__ == '__main__':
    unittest.main()
