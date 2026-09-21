#!/usr/bin/env python3
"""Patch only the retained CE material function; never regenerate other bodies."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from games.halo_ce_3925.material_sampler import hook


def patch(source):
    start = source.index('void f_00070110(xctx *restrict c)')
    end = source.find('\nvoid f_', start + 1)
    if end < 0:
        end = len(source)
    return source[:start] + hook(source[start:end]) + source[end:]


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('shard', type=Path)
    args = ap.parse_args()
    original = args.shard.read_text()
    updated = patch(original)  # Reject drift/already-patched input before writing.
    args.shard.write_text(updated)
    print('Applied four guarded material sampler groups')
