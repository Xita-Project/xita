#!/usr/bin/env python3
"""Export metadata-only progress reports and a portable per-profile treemap.

Reports describe generated source, not runtime reachability or exact matching.
No game instructions, generated C bodies, paths to local files or game assets
are exported. The HTML viewer works both offline and on a static website.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from recompiler.core.profile import load_profile

FUNCTION = re.compile(r'^void f_([0-9A-Fa-f]{8})\(xctx \*restrict c\)')
INSTRUCTION = re.compile(r'^\s*/\* ([0-9A-Fa-f]{8})  ')
UNSUPPORTED = re.compile(r'xv_unimpl\(c,\s*0x([0-9A-Fa-f]+)u,\s*"(\w+)"')
NATIVE = re.compile(r'extern int (\w+)\(xctx \*\); if \(\1\(c\)\) return;')


def generate(profile, directory, revision):
    directory = Path(directory)
    manifest = json.loads((directory / 'recomp_report.json').read_text())
    if manifest.get('profile') != profile.id or manifest.get('input_sha256') != profile.binary['sha256']:
        raise ValueError('Report profile/XBE fingerprint mismatch; regenerate with the matching game profile')
    entries = manifest.get('functions')
    if not isinstance(entries, list) or not entries or any(type(a) is not int for a in entries) or len(set(entries)) != len(entries):
        raise ValueError('Expected a nonempty, unique function list')
    base, end = profile.binary['base_address'], profile.binary['base_address'] + profile.binary['size_of_image']
    if any(not base <= a < end for a in entries):
        raise ValueError('Function address outside the profile image')
    records, hashes = {}, {}
    for path in sorted(directory.glob('code_*.c')):
        if not re.fullmatch(r'code_[0-9]+\.c', path.name):
            continue
        raw = path.read_bytes()
        hashes[path.name] = hashlib.sha256(raw).hexdigest()
        current = None
        for line in raw.decode('utf-8').splitlines():
            match = FUNCTION.match(line)
            if match:
                address = int(match[1], 16)
                if address in records:
                    raise ValueError(f'Duplicate generated function 0x{address:08X}')
                current = records[address] = dict(address=f'0x{address:08X}', name=f'f_{address:08X}',
                    source=path.name, sites=set(), unsupported={}, helpers=set())
            if current is None:
                continue
            match = INSTRUCTION.match(line)
            if match:
                current['sites'].add(match[1])
            for match in UNSUPPORTED.finditer(line):
                current['unsupported'][match[1]] = match[2]
            for match in NATIVE.finditer(line):
                current['helpers'].add(match[1])
    if set(records) != set(entries):
        raise ValueError(f'Generated chunks/report disagree: {len(set(entries)-set(records))} missing, {len(set(records)-set(entries))} unexpected functions')
    items = []
    for address in sorted(records):
        r = records[address]
        helpers = sorted(r['helpers'])
        status = 'unsupported' if r['unsupported'] else 'native' if helpers else 'translated'
        items.append(dict(id=r['address'], name=r['name'], address=r['address'], source=r['source'],
            kind='function', status=status, instructions=len(r['sites']),
            unsupported_sites=len(r['unsupported']), unsupported=sorted(set(r['unsupported'].values())),
            helpers=helpers, exact_match=None, validation=None))
    for key, kind in [('hle_used', 'hle'), ('kernel_used', 'kernel')]:
        names = manifest.get(key)
        if not isinstance(names, list) or any(not isinstance(n, str) or not re.fullmatch(r'\w+', n) for n in names):
            raise ValueError(f'Invalid {key}')
        for name in sorted(set(names)):
            items.append(dict(id=f'{kind}:{name}', name=name, kind=kind, status='boundary',
                instructions=0, exact_match=None, validation=None))
    return dict(schema_version=1, profile_id=profile.id, name=profile.name,
        input_sha256=profile.binary['sha256'], source_revision=revision,
        artifact_sha256=hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest(),
        scope='Generated functions and referenced HLE/kernel boundaries. Whole-game coverage and exact matching are not established.',
        weight='Unique emitted instruction markers per function; shared instructions can occur in more than one function.',
        summary=dict(functions=len(entries), instruction_sites=sum(i['instructions'] for i in items),
            statuses=dict(Counter(i['status'] for i in items)),
            hle=sum(i['kind']=='hle' for i in items), kernel=sum(i['kind']=='kernel' for i in items)),
        items=items)


def site(reports, destination):
    data = []
    ids = set()
    for path in reports:
        doc = json.loads(Path(path).read_text())
        if doc.get('schema_version') != 1 or not re.fullmatch(r'[a-z][a-z0-9_]*', doc.get('profile_id', '')):
            raise ValueError('Unsupported progress report')
        if doc['profile_id'] in ids:
            raise ValueError('Duplicate game profile; choose one report per revision/profile')
        ids.add(doc['profile_id'])
        data.append(doc)
    if not data:
        raise ValueError('At least one profile report is required')
    data.sort(key=lambda d: d['profile_id'])
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for name in ('index.html', 'progress.css', 'progress.js'):
        shutil.copyfile(ROOT / 'tools/progress_view' / name, destination / name)
    # External script also works with file://, unlike fetch() on local JSON.
    # Escape HTML-significant characters even though this is not inline script.
    text = json.dumps(data, ensure_ascii=True, separators=(',', ':')).replace('<', '\\u003c').replace('>', '\\u003e').replace('&', '\\u0026')
    (destination / 'reports.js').write_text('window.XITA_PROGRESS = ' + text + ';\n')
    (destination / 'reports.json').write_text(json.dumps(data, indent=2) + '\n')
    return len(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    commands = ap.add_subparsers(dest='command', required=True)
    report = commands.add_parser('report', help='derive one game report from its generated output')
    report.add_argument('--profile', required=True)
    report.add_argument('--recomp-dir', type=Path, required=True)
    report.add_argument('--revision', default='unrecorded', help='reviewed source revision used for these generated artifacts')
    report.add_argument('--output', type=Path, required=True)
    view = commands.add_parser('site', help='build the same viewer for local use or website publication')
    view.add_argument('--reports', type=Path, nargs='+', required=True)
    view.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    try:
        if args.command == 'report':
            if not re.fullmatch(r'[A-Za-z0-9._/-]{1,80}', args.revision):
                raise ValueError('Invalid source revision label')
            doc = generate(load_profile(args.profile), args.recomp_dir, args.revision)
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(doc, indent=2) + '\n')
            print(json.dumps(doc['summary']))
        else:
            count = site(args.reports, args.output)
            print(f'{count} profiles: {args.output / "index.html"}')
    except (OSError, ValueError, KeyError) as error:
        ap.error(str(error))


if __name__ == '__main__':
    main()
