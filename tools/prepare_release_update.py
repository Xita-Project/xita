#!/usr/bin/env python3
"""Prepare local GitHub Release assets. Never publishes, uploads or changes visibility."""
import argparse
import hashlib
import re
from pathlib import Path
import zipfile
from package_vpk import update_contract


def prepare(vpk, tag, version, notes, out):
    token = r'[A-Za-z0-9_-][A-Za-z0-9_.-]{0,63}'
    if any(not re.fullmatch(token, value) or '..' in value for value in (tag, version)):
        raise ValueError('Tag/version must be URL-safe tokens, at most 64 characters')
    if len(notes) > 192 or any(ord(c) < 32 or ord(c) > 126 for c in notes):
        raise ValueError('Use a release summary of at most 192 printable ASCII characters')
    with zipfile.ZipFile(vpk) as z:
        entries = z.infolist()
        if len({e.filename for e in entries}) != len(entries) or sum(e.file_size for e in entries) > 256*1024*1024:
            raise ValueError('Duplicate entries or oversized package')
        files = {e.filename: z.read(e) for e in entries}
    runtime = files.get('game-a.self', b'')
    if files.get('distribution.txt') != b'tester\n' or b'XITA-DISTRIBUTION:tester-v1' not in runtime or b'XITA-DISTRIBUTION:developer-v1' in runtime:
        raise ValueError('Only identified tester runtimes may enter the public update channel')
    if 'halo2-a.self' in files or 'release-ca.pem' not in files:
        raise ValueError('Expected a CE tester package with its TLS trust store')
    if runtime[:4] != b'SCE\0' or not 4096 <= len(runtime) <= 64*1024*1024:
        raise ValueError('Invalid runtime')
    if version.encode() not in runtime:
        raise ValueError("Release version is not present in runtime build identity")
    contract = update_contract(files)
    if files.get('update-contract.txt') != (contract+'\n').encode():
        raise ValueError('Package contract does not match assets')
    sha = hashlib.sha256(runtime).hexdigest()
    manifest = f'XITA-RELEASE-1\n{tag}\n{version}\n{len(runtime)}\n{sha}\n{contract}\ntester\n{notes}\n'
    out = Path(out)
    out.mkdir(parents=True, exist_ok=False)
    (out/'xita-runtime.self').write_bytes(runtime)
    (out/'xita-update.txt').write_text(manifest, encoding='ascii')
    return sha


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('vpk', type=Path)
    p.add_argument('--tag', required=True)
    p.add_argument('--version', required=True)
    p.add_argument('--summary', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    try:
        print(prepare(a.vpk, a.tag, a.version, a.summary, a.output))
    except (ValueError, OSError, zipfile.BadZipFile) as e:
        p.error(str(e))
