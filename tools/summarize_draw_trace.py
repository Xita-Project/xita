#!/usr/bin/env python3
"""Summarize a complete buffered render-state capture, never GPU execution cost."""
import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re


def summarize(text):
    closed = re.findall(r'hist: remote render frame (\d+) closed at (\d+): (\d+) commands, (\d+) draw-state records', text)
    if not closed:
        raise ValueError('No completed render trace')
    frame, ended, commands, reported = map(int, closed[-1])
    buffer = re.findall(r'\[draw-trace-buffer\] frame ' + str(frame) + r' bytes (\d+) dropped-lines (\d+) allocated (\d+)', text)
    if frame != ended or not buffer or buffer[-1][1:] != ('0', '1'):
        raise ValueError('Incomplete, dropped or unallocated trace buffer')
    draws = {}
    pattern = (r'\[draw-state\] frame (\d+) cmd (\d+) pass (\d+) vs (\S+) ps (\S+) key (\S+) '
               r'tex-mask (\S+) previous (\S+) blend (\S+) z (\S+) mask (\S+) atest (\S+)')
    for match in re.finditer(pattern, text):
        f, cmd, render_pass, vs, ps, key, mask, previous, blend, depth, color, atest = match.groups()
        if int(f) != frame:
            continue
        cmd = int(cmd)
        if cmd in draws or cmd >= commands:
            raise ValueError('Duplicate or out-of-range draw command')
        draws[cmd] = dict(command=cmd, render_pass=int(render_pass), vs=vs, ps=ps, key=key,
                          texture_mask=mask, previous=previous, blend=blend, depth=depth,
                          color_mask=color, alpha_test=atest, textures={}, constants={})
    if len(draws) != reported or not draws:
        raise ValueError(f'Reported {reported} draws, recovered {len(draws)}')
    for match in re.finditer(r'\[draw-texture\] frame (\d+) cmd (\d+) stage (\d+) data (\S+) size (\d+)/(\d+) type (\S+) address (\S+)', text):
        f, cmd, stage, data, width, height, kind, address = match.groups()
        if int(f) == frame:
            draws[int(cmd)]['textures'][stage] = dict(data=data, width=int(width), height=int(height), type=kind, address=address)
    for match in re.finditer(r'\[draw-constant\] frame (\d+) cmd (\d+) psc (\d+) ([^\n]+)', text):
        f, cmd, row, values = match.groups()
        if int(f) == frame:
            # Preserve decimal text; printed values are diagnostic evidence,
            # not a bitwise proof for a shader specialization.
            draws[int(cmd)]['constants'][row] = values.split()
    groups = defaultdict(list)
    for draw in draws.values():
        groups[(draw['ps'], draw['key'], draw['vs'], draw['render_pass'])].append(draw)
    summary = []
    for (ps, key, vs, render_pass), entries in groups.items():
        layouts = Counter(tuple((stage, t['width'], t['height'], t['type']) for stage, t in sorted(d['textures'].items())) for d in entries)
        constants = defaultdict(set)
        for d in entries:
            for row, values in d['constants'].items():
                constants[row].add(tuple(values))
        summary.append(dict(ps=ps, key=key, vs=vs, render_pass=render_pass, draws=len(entries),
                            commands=[d['command'] for d in entries],
                            blends=dict(Counter(d['blend'] for d in entries)),
                            texture_layouts=[dict(stages=layout, draws=count) for layout, count in layouts.most_common()],
                            constant_values={row: sorted(values) for row, values in constants.items()}))
    return dict(frame=frame, commands=commands, draw_records=reported, captured_bytes=int(buffer[-1][0]),
                caveat='Recorded state only: counts and texture sizes do not measure coverage or GPU time.',
                groups=sorted(summary, key=lambda g: -g['draws']), draws=list(draws.values()))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(summarize(args.log.read_text(errors='replace')), indent=2))
    except (ValueError, KeyError) as error:
        parser.exit(1, f'Invalid trace: {error}\n')
