#!/usr/bin/env python3
"""Inventory release risks locally; this is not a legal clearance or a publisher.

Default: inspect tracked paths and their current bytes. --history also checks
reachable historical blobs. Reports list locations, never matched secret values.
--export writes an isolated source-review directory, excluding flagged material
and unknown file types. It never changes Git, deletes originals, or publishes.
"""
from pathlib import Path, PurePosixPath
import argparse
import collections
import hashlib
import json
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GENERATED = re.compile(r'(^|/)(code_[^/]*\.c|xv_fn_table\.c|xv_stubs_default\.c|xv_recomp_protos\.h|recomp_report\.json)$')
SECRET_PATTERNS = {
    'private-key': re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'),
    'github-token': re.compile(rb'\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{50,})\b'),
    'aws-access-key': re.compile(rb'\bAKIA[A-Z0-9]{16}\b'),
    'slack-token': re.compile(rb'\bxox[baprs]-[0-9A-Za-z-]{24,}\b'),
    'openai-token': re.compile(rb'\bsk-(?:proj-)?[A-Za-z0-9_-]{40,}\b'),
}


def path_risks(name):
    p = PurePosixPath(name)
    low = name.lower()
    reasons = []
    if GENERATED.search(name) or name == 'recomp/kernel/xk_clip.c': reasons.append('generated-game-code')
    if p.suffix.lower() in {'.xbe', '.xiso', '.iso', '.map', '.psp2dmp', '.suprx', '.skprx', '.vpk', '.elf', '.velf', '.o', '.a', '.exe', '.dll'} or p.name.lower() == 'eboot.bin':
        reasons.append('game-build-dump-or-sdk-binary')
    if p.suffix.lower() in {'.bin', '.gxp'}: reasons.append('binary-provenance-review')
    if low.startswith('shaders/halo') or low.startswith('shaders/ps_') or low.startswith('shaders/psdefs/') or name in {'halo_shader_0.cg', 'shaders/xv_layouts.h', 'shaders/xv_ps_table.h', 'shaders/xv_ps_gxp.h', 'shaders/xv_hud_gxp.h', 'shaders/xv_vs_gxp.h'}:
        reasons.append('translated-or-captured-game-shader')
    if name in {'game_manifest.json', 'halo_symbols.json'} or low.startswith('haloce/'):
        reasons.append('game-input-provenance-review')
    if low.startswith('local/'):
        reasons.append('private-runtime-or-local-data')
    if low.startswith(('assets/', 'site/', 'sce_sys/')) or p.suffix.lower() in {'.png', '.jpg', '.jpeg', '.webp', '.mp4', '.wav', '.ogg', '.mp3'}:
        reasons.append('artwork-or-media-provenance-review')
    if p.suffix.lower() in {'.log', '.sav'} or any(x.lower() in {'save', 'saves', '.env', '.claude', '.codex'} for x in p.parts):
        reasons.append('private-runtime-or-local-data')
    return reasons


def content_risks(data):
    reasons = [name for name, pattern in SECRET_PATTERNS.items() if pattern.search(data)]
    if b'\0' not in data and re.search(rb'(?<![0-9A-Fa-f])[0-9A-Fa-f]{128,}(?![0-9A-Fa-f])', data):
        reasons.append('long-hex-payload-review')
    return reasons


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def inspect_paths(names):
    findings = []
    for name in sorted(set(names)):
        p = ROOT / name
        reasons = path_risks(name)
        if p.is_symlink(): reasons.append('symlink-review')
        elif p.is_file():
            data=p.read_bytes(); reasons += content_risks(data)
            if b'\0' in data and p.suffix in {'.c','.h','.py','.sh','.md','.cg','.txt'}:
                reasons.append('binary-in-source-file')
        else: reasons.append('missing-file')
        if reasons: findings.append({'path': name, 'reasons': sorted(set(reasons))})
    return findings


def inspect_history():
    records = []
    objects = git('rev-list', '--objects', '--all').decode().splitlines()
    proc = subprocess.Popen(['git', '-C', str(ROOT), 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    try:
        for row in objects:
            oid, _, name = row.partition(' ')
            proc.stdin.write((oid + '\n').encode()); proc.stdin.flush()
            header = proc.stdout.readline().decode().split()
            if len(header) != 3: raise RuntimeError('Unexpected git object response')
            data = proc.stdout.read(int(header[2]))
            if proc.stdout.read(1) != b'\n': raise RuntimeError('Truncated git object')
            if header[1] != 'blob': continue
            reasons = sorted(set(path_risks(name) + content_risks(data)))
            if reasons: records.append({'object': oid, 'path': name, 'reasons': reasons})
    finally:
        proc.stdin.close(); proc.stdout.close()
        if proc.wait(): raise RuntimeError('git cat-file failed')
    return records


def export_review(destination):
    destination = destination.resolve()
    if destination.exists(): raise ValueError('Export destination must not exist')
    if destination == ROOT or ROOT in destination.parents:
        raise ValueError('Keep the source review export outside this development checkout')
    names = git('ls-files', '--cached', '--others', '--exclude-standard', '-z').decode().split('\0')
    names = [x for x in names if x]
    flagged = {x['path']: x['reasons'] for x in inspect_paths(names)}
    manifest, excluded = [], []
    destination.mkdir(parents=True)
    for name in sorted(set(names)):
        p = ROOT / name
        approved_type = p.suffix in {'.c', '.h', '.py', '.sh', '.md', '.cg', '.ps1', '.mk', '.inc'} or (name.startswith('games/') and p.name == 'profile.json') or p.name in {'Makefile', 'LICENSE', 'NOTICE', '.gitignore', '.gitattributes'} or name.startswith('LICENSES/')
        reasons = flagged.get(name, []) or ([] if approved_type else ['unreviewed-file-type'])
        if reasons:
            excluded.append({'path': name, 'reasons': reasons}); continue
        target = destination / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, target)
        manifest.append({'path': name, 'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
    # Deliberately not a Git repository or a claim that Halo builds without the
    # excluded game-derived inputs. The private originals remain intact.
    (destination/'SOURCE_REVIEW.json').write_text(json.dumps({'status':'review only; not legal clearance or a complete Halo build', 'files':manifest, 'excluded':excluded},indent=2)+'\n')
    return {'path':str(destination), 'included':len(manifest), 'excluded':len(excluded)}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--history', action='store_true')
    p.add_argument('--json', type=Path)
    p.add_argument('--export', type=Path)
    args = p.parse_args()
    paths = [x for x in git('ls-files','-z').decode().split('\0') if x]
    report = {'tracked_files':len(paths), 'findings':inspect_paths(paths), 'limitations':'Heuristics and a provenance inventory; no guarantee of legal compliance or absence of secrets.'}
    if args.history: report['history_findings'] = inspect_history()
    if args.export: report['source_review_export'] = export_review(args.export)
    if args.json: args.json.write_text(json.dumps(report,indent=2)+'\n')
    counts = collections.Counter(reason for x in report['findings'] for reason in x['reasons'])
    print(json.dumps({'tracked_files':len(paths),'flagged_files':len(report['findings']),'categories':dict(counts),'historical_blobs_flagged':len(report.get('history_findings',[])),'source_review_export':report.get('source_review_export')},indent=2))
    return 1 if report['findings'] or report.get('history_findings') else 0


if __name__ == '__main__':
    raise SystemExit(main())
