#!/usr/bin/env python3
"""Inspect selected exFAT file allocations without writing to the source.

This is a bounded diagnostic, not fsck or a TexFAT repair implementation.
Use an unmounted volume or a stable image. Paths are exact, case-sensitive
directory names; the exFAT up-case table is deliberately not approximated.
Format reference: https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification
"""
import argparse
import functools
import itertools
import json
import struct
import sys
from pathlib import PurePosixPath

MAX_READ = 64 * 1024 * 1024
MAX_CHAIN = 131072
MAX_DIRECTORY = 16 * 1024 * 1024


class AuditError(ValueError):
    pass


def u16(b, offset):
    return struct.unpack_from('<H', b, offset)[0]


def u32(b, offset):
    return struct.unpack_from('<I', b, offset)[0]


def u64(b, offset):
    return struct.unpack_from('<Q', b, offset)[0]


def checksum(data, bits, skip=()):
    value, mask = 0, (1 << bits) - 1
    for i, byte in enumerate(data):
        if i not in skip:
            value = (((value >> 1) | (value << (bits - 1))) + byte) & mask
    return value


class Volume:
    def __init__(self, stream, offset=0):
        if offset < 0:
            raise AuditError('negative volume offset')
        self.stream, self.offset, self.read_bytes = stream, offset, 0
        self.length = None
        boot = self.read(0, 512)
        if boot[3:11] != b'EXFAT   ' or boot[510:512] != b'\x55\xaa':
            raise AuditError('source offset is not an exFAT boot sector')
        if not 9 <= boot[108] <= 12 or boot[108] + boot[109] > 25:
            raise AuditError('invalid sector/cluster shifts')
        self.sector = 1 << boot[108]
        self.cluster = self.sector << boot[109]
        self.length = u64(boot, 72) * self.sector
        self.fat_offset, self.fat_length = u32(boot, 80), u32(boot, 84)
        self.heap, self.count, self.root = u32(boot, 88), u32(boot, 92), u32(boot, 96)
        self.flags, self.fats = u16(boot, 106), boot[110]
        self.active = self.flags & 1
        if u16(boot, 104) != 0x100:
            raise AuditError('only exFAT revision 1.00 is supported')
        if self.fats not in (1, 2) or self.active >= self.fats:
            raise AuditError('invalid FAT count/active FAT')
        if not 1 <= self.count <= 0xfffffff5:
            raise AuditError('invalid cluster count')
        if self.fat_offset < 24 or self.fat_length * self.sector < (self.count + 2) * 4:
            raise AuditError('invalid FAT geometry')
        if self.fat_offset + self.fats * self.fat_length > self.heap:
            raise AuditError('FAT overlaps cluster heap')
        if self.heap * self.sector + self.count * self.cluster > self.length:
            raise AuditError('cluster heap exceeds declared volume')
        self.cluster_offset(self.root)
        # Boot checksum excludes mutable volume flags and percent-in-use.
        region = self.read(0, 12 * self.sector)
        expected = checksum(region[:11 * self.sector], 32, (106, 107, 112))
        if region[11 * self.sector:] != struct.pack('<I', expected) * (self.sector // 4):
            raise AuditError('main boot-region checksum mismatch')

    def read(self, offset, size):
        if offset < 0 or size < 0 or (self.length is not None and offset + size > self.length):
            raise AuditError('read outside declared volume')
        self.read_bytes += size
        if self.read_bytes > MAX_READ:
            raise AuditError('metadata read budget exceeded')
        self.stream.seek(self.offset + offset)
        data = self.stream.read(size)
        if len(data) != size:
            raise AuditError('truncated source')
        return data

    def cluster_offset(self, cluster):
        if not 2 <= cluster < self.count + 2:
            raise AuditError(f'cluster out of range: {cluster:#x}')
        return self.heap * self.sector + (cluster - 2) * self.cluster

    @functools.lru_cache(maxsize=256)
    def fat_sector(self, fat, sector):
        return self.read((self.fat_offset + fat * self.fat_length + sector) * self.sector,
                         self.sector)

    def next_cluster(self, cluster):
        self.cluster_offset(cluster)
        sector, offset = divmod(cluster * 4, self.sector)
        return u32(self.fat_sector(self.active, sector), offset)

    def chain(self, first, length=None, contiguous=False):
        needed = None if length is None else (length + self.cluster - 1) // self.cluster
        if needed == 0:
            if first or contiguous:
                raise AuditError('invalid empty allocation')
            return []
        self.cluster_offset(first)
        if needed is not None and needed > MAX_CHAIN:
            raise AuditError('allocation exceeds cluster budget')
        if contiguous:
            if needed is None:
                raise AuditError('contiguous allocation requires a length')
            self.cluster_offset(first + needed - 1)
            return list(range(first, first + needed))
        result, seen, current = [], set(), first
        while True:
            self.cluster_offset(current)
            if current in seen:
                raise AuditError('cycle in active FAT chain')
            if len(result) >= MAX_CHAIN:
                raise AuditError('FAT chain exceeds cluster budget')
            seen.add(current)
            result.append(current)
            current = self.next_cluster(current)
            if current == 0xffffffff:
                break
            if needed is not None and len(result) >= needed:
                raise AuditError('FAT chain longer than declared allocation')
        if needed is not None and len(result) != needed:
            raise AuditError('FAT chain shorter than declared allocation')
        return result

    def read_allocation(self, clusters, length):
        if length > MAX_DIRECTORY:
            raise AuditError('directory/bitmap exceeds diagnostic limit')
        result = bytearray()
        for cluster in clusters:
            amount = min(self.cluster, length - len(result))
            if amount <= 0:
                break
            result.extend(self.read(self.cluster_offset(cluster), amount))
        if len(result) != length:
            raise AuditError('short allocation')
        return bytes(result)

    def directory(self, clusters, length=None):
        length = len(clusters) * self.cluster if length is None else length
        if length % 32:
            raise AuditError('unaligned directory length')
        data = self.read_allocation(clusters, length)
        files, bitmaps, position = {}, {}, 0
        while position + 32 <= len(data):
            entry = data[position:position + 32]
            position += 32
            kind = entry[0]
            if kind == 0:
                break
            if not kind & 0x80:
                continue
            if kind == 0x81:
                identity = entry[1] & 1
                if identity in bitmaps or identity >= self.fats or entry[1] & ~1:
                    raise AuditError('invalid/duplicate allocation bitmap')
                bitmaps[identity] = dict(first=u32(entry, 20), length=u64(entry, 24))
            elif kind == 0x85:
                secondary = entry[1]
                if secondary < 2 or position + 32 * secondary > len(data):
                    raise AuditError('truncated file entry set')
                group = entry + data[position:position + 32 * secondary]
                position += 32 * secondary
                if checksum(group, 16, (2, 3)) != u16(entry, 2):
                    raise AuditError('file entry-set checksum mismatch')
                stream = group[32:64]
                if stream[0] != 0xc0 or not stream[1] & 1 or stream[1] & ~3:
                    raise AuditError('invalid stream extension')
                if not stream[3] or u64(stream, 8) > u64(stream, 24):
                    raise AuditError('invalid stream name/valid-data length')
                names = []
                for start in range(64, len(group), 32):
                    item = group[start:start + 32]
                    if item[0] == 0xc1:
                        names.append(item[2:32])
                    elif not item[0] & 0x20:
                        raise AuditError('unsupported critical file secondary entry')
                encoded = b''.join(names)
                if len(encoded) < stream[3] * 2:
                    raise AuditError('truncated filename')
                try:
                    name = encoded[:stream[3] * 2].decode('utf-16-le')
                except UnicodeError as exc:
                    raise AuditError('invalid UTF-16 filename') from exc
                if name in files or '\x00' in name or '/' in name:
                    raise AuditError('duplicate/invalid filename')
                files[name] = dict(first=u32(stream, 20), length=u64(stream, 24),
                                   contiguous=bool(stream[1] & 2),
                                   directory=bool(u16(entry, 4) & 0x10))
            elif kind not in (0x82, 0x83) and not kind & 0x20:
                raise AuditError(f'unsupported critical directory entry {kind:#x}')
        return files, bitmaps

    def audit(self, paths):
        if len({str(PurePosixPath(p)) for p in paths}) != len(paths):
            raise AuditError('duplicate selected path')
        root_chain = self.chain(self.root)
        root_files, bitmaps = self.directory(root_chain)
        if set(bitmaps) != set(range(self.fats)):
            raise AuditError('missing allocation bitmap entry')
        active_bitmap = bitmaps[self.active]
        bitmap_chain = self.chain(**active_bitmap)
        required = (self.count + 7) // 8
        if active_bitmap['length'] < required:
            raise AuditError('active bitmap too short')
        bitmap = self.read_allocation(bitmap_chain, active_bitmap['length'])
        allocations, findings = [], []
        for path in paths:
            components = PurePosixPath(path).parts
            if not components or any(p in ('/', '..', '.') for p in components) or '.' in path.split('/'):
                raise AuditError('use a nonempty volume-relative path without dot components')
            files = root_files
            for i, part in enumerate(components):
                if part not in files:
                    raise AuditError(f'path not found (exact case required): {path}')
                info = files[part]
                clusters = self.chain(info['first'], info['length'], info['contiguous'])
                if i + 1 < len(components):
                    if not info['directory']:
                        raise AuditError(f'non-directory path component: {part}')
                    files, _ = self.directory(clusters, info['length'])
            free = [c for c in clusters if not (bitmap[(c - 2) // 8] >> ((c - 2) % 8)) & 1]
            allocations.append(dict(path=path, **info, clusters=clusters,
                                    marked_free_in_active_bitmap=free))
            if free:
                findings.append(dict(kind='referenced_clusters_marked_free', path=path, clusters=free))
        for left, right in itertools.combinations(allocations, 2):
            common = sorted(set(left['clusters']) & set(right['clusters']))
            if common:
                findings.append(dict(kind='selected_file_cluster_overlap',
                                     paths=[left['path'], right['path']], clusters=common))
        return dict(read_only=True, scope='Selected files only; not a full filesystem check.',
                    inactive_state='Inactive FAT/bitmap is stale by specification, not corruption evidence.',
                    sector_bytes=self.sector, cluster_bytes=self.cluster, fat_count=self.fats,
                    active_fat=self.active, volume_dirty=bool(self.flags & 2),
                    media_failure_flag=bool(self.flags & 4), bitmap_entries=bitmaps,
                    files=allocations, findings=findings, bytes_read=self.read_bytes)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', help='Unmounted exFAT volume device or stable image (opened rb)')
    parser.add_argument('paths', nargs='+', help='Exact volume-relative paths to inspect')
    parser.add_argument('--offset', type=lambda s: int(s, 0), default=0,
                        help='Byte offset of volume within image; default 0')
    args = parser.parse_args(argv)
    try:
        with open(args.source, 'rb') as source:
            report = Volume(source, args.offset).audit(args.paths)
    except (OSError, AuditError) as exc:
        print(f'audit stopped: {exc}', file=sys.stderr)
        return 2
    print(json.dumps(report, indent=2))
    return 1 if report['findings'] else 0


if __name__ == '__main__':
    raise SystemExit(main())
