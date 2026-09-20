#!/usr/bin/env python3
"""Inventory owned emitted-code matrix-offset references; not a dataflow proof.

Output contains addresses and counts, never generated instruction bodies.
References through returned/stored pointers require a separate caller audit.
"""
import argparse
import json
from pathlib import Path
import re


def inventory(directory):
    rows = []
    escapes = {'0008B290': {}, '00172DE0': {}}
    for path in sorted(directory.glob('code_*.c')):
        text = path.read_text()
        starts = list(re.finditer(r'^void f_([0-9A-F]{8})\(xctx \*restrict c\)\n\{', text, re.M))
        for i, match in enumerate(starts):
            body = text[match.end():starts[i + 1].start() if i + 1 < len(starts) else len(text)]
            # Emitted closures and alternate entries duplicate guest call sites.
            # Retain their owners, but count unique original instruction addresses.
            for call in re.finditer(r'/\* ([0-9A-F]{8})  call ([0-9A-F]+)h \*/', body):
                target = f'{int(call[2], 16):08X}'
                if target in escapes:
                    owners = escapes[target].setdefault(call[1], set())
                    owners.add((path.name, match[1]))
            references = body.count('0x1A2u')
            if references:
                rows.append(dict(function=match[1], unit=path.name, references=references,
                    direct_calls=sorted(set(re.findall(r'\bf_([0-9A-F]{8})\(c\)', body)))))
    return dict(scope='Lexical offset inventory, not reader/writer classification or complete alias coverage',
                functions=len(rows), references=sum(r['references'] for r in rows), rows=rows,
                escaped_pointer_call_sites={target: [dict(site=site,
                    emitted_owners=[dict(unit=unit, function=owner)
                                    for unit, owner in sorted(owners)])
                    for site, owners in sorted(sites.items())]
                    for target, sites in escapes.items()})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('recomp', type=Path, help='Private generated-code directory')
    args = parser.parse_args()
    if not args.recomp.is_dir():
        parser.error('Generated-code directory does not exist')
    result = inventory(args.recomp)
    if not result['functions']:
        parser.error('No expected generated matrix references found')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
