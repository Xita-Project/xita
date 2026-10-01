#!/usr/bin/env python3
"""Recover an erased Halo 3925 profile header from its matching cached checkpoint.

Writes a new local file only. The original profile/cache are never modified.
This is an offline repair for the old short-file bug, not an automatic migration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

STATE_BYTES = 0x345000
PROFILE_BYTES = 0x380000
ERASED_BYTES = 0x4000
CRC_OFFSET = 0x148
BUILD = b"01.10.12.2276"


def recover(profile: bytes, cache: bytes) -> bytes:
    for name, data in (("profile", profile), ("cache", cache)):
        if len(data) not in (STATE_BYTES, PROFILE_BYTES):
            raise ValueError(f"{name} must contain a complete known-size checkpoint")
        if any(data[STATE_BYTES:]):
            raise ValueError(f"{name} contains unexpected data beyond the checkpoint")
    if any(profile[:ERASED_BYTES]):
        raise ValueError("profile does not have the old completely erased 16 KiB header")
    if profile[ERASED_BYTES:STATE_BYTES] != cache[ERASED_BYTES:STATE_BYTES]:
        raise ValueError("cache and profile checkpoint payloads do not match")
    if struct.unpack_from("<I", cache)[0] != 0x1FD7A64F:
        raise ValueError("cache has an unsupported game-state layout")
    if cache[0x104:0x124].split(b"\0", 1)[0] != BUILD:
        raise ValueError("cache has an unsupported Halo build")
    name = cache[4:0x104].split(b"\0", 1)[0]
    if not name.startswith(b"levels\\") or len(name) >= 0x100:
        raise ValueError("cache has no valid campaign map path")
    # The cache has raw game state. Profile saving (0x315D0) additionally stores
    # CRC32 at +0x148. 0x31700 checks it over 0x345000 bytes with this word zero.
    # Halo's 0xA9630 starts at FFFFFFFF and omits zlib's final inversion.
    result = bytearray(cache[:STATE_BYTES])
    struct.pack_into("<I", result, CRC_OFFSET, 0)
    checksum = zlib.crc32(result) ^ 0xFFFFFFFF
    struct.pack_into("<I", result, CRC_OFFSET, checksum)
    result.extend(bytes(PROFILE_BYTES - STATE_BYTES))
    return bytes(result)


def recover_file(profile: Path, cache: Path, output: Path) -> dict:
    damaged, cached = profile.read_bytes(), cache.read_bytes()
    recovered = recover(damaged, cached)
    # Exclusive creation also rejects either input as the output, including
    # existing symlinks. A typo must not overwrite the only surviving save.
    with output.open("xb") as stream:
        stream.write(recovered)
    return {
        "profile_sha256": hashlib.sha256(damaged).hexdigest(),
        "cache_sha256": hashlib.sha256(cached).hexdigest(),
        "output": str(output),
        "output_sha256": hashlib.sha256(recovered).hexdigest(),
        "bytes": len(recovered),
        "crc32": f"{struct.unpack_from('<I', recovered, CRC_OFFSET)[0]:08X}",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path, help="damaged profile savegame.bin")
    parser.add_argument("cache", type=Path, help="matching cached savegame.bin")
    parser.add_argument("output", type=Path, help="new local output file")
    args = parser.parse_args()
    try:
        result = recover_file(args.profile, args.cache, args.output)
    except (ValueError, OSError) as exc:
        parser.exit(1, f"Recovery refused: {exc}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
