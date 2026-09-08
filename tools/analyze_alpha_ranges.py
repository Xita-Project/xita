#!/usr/bin/env python3
"""Summarize observation-only uploaded-alpha histograms; never rank GPU cost."""
import argparse
import collections
import json
from pathlib import Path
import re

PATTERN = re.compile(
    r"\[alpha-range\] frame (\d+) cmd (\d+) indices (\d+) texture ([0-9A-F]+) "
    r"atest ([0-9A-F]+) range (\d+)\.\.(\d+) valid (\d+) opaque (\d+) "
    r"candidate (\d+) addr (\d+)/(\d+)"
)


def potential_pass(row):
    """Conservative screening only; allow a full alpha byte at the cutoff."""
    if not row["valid"] or row["address_u"] not in (1, 2, 3, 5) or row["address_v"] not in (1, 2, 3, 5):
        return False
    lo, hi, ref, func = row["min"], row["max"], row["reference"], row["function"]
    return ((func == 1 and hi < ref - 1) or
            (func == 3 and hi <= ref - 1) or
            (func == 4 and lo > ref + 1) or
            (func == 5 and (hi < ref - 1 or lo > ref + 1)) or
            (func == 6 and lo >= ref + 1))


def analyze(text):
    frames = collections.defaultdict(list)
    for m in PATTERN.finditer(text):
        v = list(m.groups())
        frame, command, indices = map(int, v[:3])
        atest = int(v[4], 16)
        row = dict(command=command, indices=indices, texture=v[3], atest=v[4],
                   min=int(v[5]), max=int(v[6]), valid=int(v[7]),
                   opaque=int(v[8]), candidate=int(v[9]),
                   address_u=int(v[10]), address_v=int(v[11]),
                   reference=atest & 255, function=(atest >> 8) & 7,
                   enabled=bool(atest & (1 << 16)))
        row["remaining"] = row["enabled"] and row["function"] != 7 and not row["opaque"]
        row["potential_interval_pass"] = row["remaining"] and potential_pass(row)
        frames[frame].append(row)
    result = []
    for frame, rows in sorted(frames.items()):
        remaining = [r for r in rows if r["remaining"]]
        potential = [r for r in rows if r["potential_interval_pass"]]
        result.append(dict(frame=frame, material_draws=len(rows),
                           existing_opaque_draws=sum(r["opaque"] for r in rows),
                           remaining_draws=len(remaining),
                           remaining_indices=sum(r["indices"] for r in remaining),
                           potential_draws=len(potential),
                           potential_indices=sum(r["indices"] for r in potential),
                           remaining_ranges=dict(collections.Counter(
                               f'{r["min"]}..{r["max"]}' if r["valid"] else "unknown"
                               for r in remaining)), draws=rows))
    if not result:
        raise ValueError("No alpha-range histogram observations in this log")
    return dict(scope="Captured material draws only; candidate counts are not measured GPU savings", frames=result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = analyze(args.log.read_text(errors="replace"))
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps([{k: v for k, v in f.items() if k != "draws"} for f in report["frames"]], indent=2))
