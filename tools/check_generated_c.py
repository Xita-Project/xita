#!/usr/bin/env python3
"""Syntax-check a generated game directory without linking a game runtime."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    files = sorted(args.directory.glob('*.c'))
    if not files or args.jobs < 1:
        parser.error('expected generated .c files and a positive job count')
    runtime = Path(__file__).resolve().parents[1] / 'recomp'

    def check(path):
        result = subprocess.run([args.cc, '-std=gnu11', '-fsyntax-only', '-w',
                                 '-I', str(runtime), '-I', str(args.directory), str(path)],
                                capture_output=True, text=True)
        return {'file': path.name, 'returncode': result.returncode, 'errors': result.stderr}

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(check, files))
    if args.report:
        args.report.write_text(json.dumps(results, indent=2) + '\n')
    failures = [r for r in results if r['returncode']]
    print(f'Syntax check: {len(files) - len(failures)} / {len(files)} passed')
    for result in failures:
        print(result['file'], result['errors'])
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
