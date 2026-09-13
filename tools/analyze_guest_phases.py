#!/usr/bin/env python3
"""Read completed phase windows, preserving overlap and diagnostic limits."""
import argparse
import json
from pathlib import Path
import re

HEADER = re.compile(r"\[guest-phase\] (\d+) frames end-frame (\d+) window-us (\d+) dropped (\d+) invalid (\d+);")
ROW = re.compile(r"\[guest-phase\] ([0-9A-F]{8}) (\w+) calls (\d+) active-us (\d+) self-us (\d+) parked-us (\d+) parked-self-us (\d+)$")
END = re.compile(r"\[guest-phase\] report-us (\d+)$")


def parse_windows(text):
    windows, errors, current = [], [], None
    for lineno, line in enumerate(text.splitlines(), 1):
        if m := HEADER.search(line):
            if current is not None:
                errors.append(f"Incomplete window starting at line {current['line']}")
            frames, end_frame, elapsed, dropped, invalid = map(int, m.groups())
            current = dict(line=lineno, frames=frames, end_frame=end_frame,
                           window_us=elapsed, dropped=dropped, invalid=invalid, rows=[])
        elif current is not None and (m := ROW.search(line)):
            address, name, *values = m.groups()
            keys = ["calls", "active_us", "self_us", "parked_us", "parked_self_us"]
            row = dict(address=address, name=name, **dict(zip(keys, map(int, values))))
            if any(r["address"] == address for r in current["rows"]) or row["self_us"] > row["active_us"] or row["parked_self_us"] > row["parked_us"]:
                current["invalid"] += 1
            current["rows"].append(row)
        elif current is not None and (m := END.search(line)):
            current["report_us"] = int(m.group(1))
            current["usable"] = current["frames"] > 0 and not current["dropped"] and not current["invalid"]
            windows.append(current)
            current = None
        elif "[guest-phase]" in line and current is not None:
            # An unrecognized phase record may be truncated or from another
            # session. Never silently rank an apparently complete partial set.
            current["invalid"] += 1
    if current is not None:
        errors.append(f"Incomplete window starting at line {current['line']}")
    return windows, errors


def summarize(windows):
    good = [w for w in windows if w["usable"]]
    frames = sum(w["frames"] for w in good)
    totals = {}
    for window in good:
        for row in window["rows"]:
            key = (row["address"], row["name"])
            values = totals.setdefault(key, dict(calls=0, active_us=0, self_us=0, parked_us=0, parked_self_us=0))
            for field in values:
                values[field] += row[field]
    ranked = []
    for (address, name), values in totals.items():
        ranked.append(dict(address=address, name=name, calls=values["calls"],
            **{field.replace("_us", "_ms_per_frame"): value / frames / 1000
               for field, value in values.items() if field != "calls"}))
    ranked.sort(key=lambda row: row["self_ms_per_frame"], reverse=True)
    return dict(usable_windows=len(good), rejected_windows=len(windows)-len(good), frames=frames,
                report_ms_per_frame=sum(w["report_us"] for w in good)/frames/1000 if frames else 0,
                ranked_by_self=ranked,
                limits="Scheduled elapsed includes native blocking and host preemption; inclusive rows overlap. Mixed maps/views/settings are not an optimization comparison.")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("log", type=Path)
    ap.add_argument("--last", type=int, default=5, help="last N complete windows; 0 means all")
    args = ap.parse_args()
    if args.last < 0:
        ap.error("--last must be nonnegative")
    windows, errors = parse_windows(args.log.read_text(errors="replace"))
    selected = windows[-args.last:] if args.last else windows
    print(json.dumps(dict(log=str(args.log), errors=errors, **summarize(selected)), indent=2))
    return 0 if any(w["usable"] for w in selected) else 1


if __name__ == "__main__":
    raise SystemExit(main())
