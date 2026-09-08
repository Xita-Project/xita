#!/usr/bin/env python3
"""Add only the verified flare hooks to an existing generated-code snapshot.

This avoids regenerating unrelated guest code when testing an isolated candidate.
The regular emitter emits the same hooks. --check verifies idempotence without
writing. Source outside these insertions is preserved byte for byte.
"""
import argparse
import hashlib
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from halo_flare_hooks import IMAGE_SHA256, ENTRY_HOOK, BARRIERS, barrier_line


def add_hooks(source):
    # Remove just our own lines so reruns cannot duplicate a guard.
    plain = source.replace(ENTRY_HOOK + '\n', '')
    for address in BARRIERS:
        plain = plain.replace(barrier_line(address) + '\n', '')
    match = re.search(r'void f_00060460\(xctx \*restrict c\)\n\{\n.*?'
                      r'    uint8_t \*const imgb_ = g_img_base; \(void\)imgb_;\n'
                      r'(?:    XV_FN\(0x00060460u\);\n)?', plain, re.S)
    if match:
        plain = plain[:match.end()] + ENTRY_HOOK + '\n' + plain[match.end():]
    for address in BARRIERS:
        needle = f'    /* {address:08X}  '
        plain = plain.replace(needle, barrier_line(address) + '\n' + needle)
    return plain


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('--xbe', type=Path, default=ROOT/'haloce/default.xbe')
    p.add_argument('--check', action='store_true')
    args = p.parse_args()
    if hashlib.sha256(args.xbe.read_bytes()).hexdigest() != IMAGE_SHA256:
        raise SystemExit('Unsupported XBE: no files changed')
    changed = []
    seen = set()
    for path in sorted(args.source.glob('code_*.c')):
        before = path.read_text()
        after = add_hooks(before)
        if ENTRY_HOOK in after:
            seen.add(0x60460)
        for address in BARRIERS:
            if f'/* {address:08X}  ' in after:
                seen.add(address)
        if before != after:
            changed.append((path, after))
    if seen != set(BARRIERS) | {0x60460}:
        raise SystemExit(f'Incomplete generated snapshot: seen {sorted(seen)}')
    if args.check and changed:
        raise SystemExit(f'Missing hooks: {[path.name for path, _ in changed]}')
    # Validate the complete snapshot before modifying any source file.
    if not args.check:
        for path, after in changed:
            path.write_text(after)
    print(f'Flare hooks: {len(changed)} files changed; all {len(seen)} sites present')


if __name__ == '__main__':
    main()
