#!/usr/bin/env python3
"""Embed the release version and source revision; rewrite only on a change."""
import argparse
import json
import re
import subprocess
from pathlib import Path


def metadata(root, revision_override=None):
    data = json.loads((root / "version.json").read_text(encoding="utf-8"))
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:-[a-z0-9.]+)?", data["version"]):
        raise ValueError("Invalid release version")
    if len(data["version"]) > 16 or not re.fullmatch(r"[0-9]{2}\.[0-9]{2}", data["sfo_version"]):
        raise ValueError("Version exceeds UI bounds or invalid Vita APP_VER")
    if revision_override is not None:
        if not re.fullmatch(r"[a-f0-9]{7,8}\+?", revision_override):
            raise ValueError("Invalid source revision")
        data["revision"] = revision_override
        return data
    try:
        revision = subprocess.check_output(["git", "rev-parse", "--short=7", "HEAD"], cwd=root, stderr=subprocess.DEVNULL, text=True).strip()
        dirty = subprocess.check_output(["git", "status", "--porcelain", "--untracked-files=no"], cwd=root, text=True).strip()
        if dirty:
            revision += "+"
    except subprocess.CalledProcessError:
        revision = "source"
    data["revision"] = revision
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument("--output", type=Path)
    p.add_argument("--sfo", action="store_true")
    p.add_argument("--revision", help="recorded source commit for a staged build")
    a = p.parse_args()
    data = metadata(a.root,a.revision)
    if a.sfo:
        print(data["sfo_version"])
    if a.output:
        text = "/* Generated; '+' marks modified tracked source. */\n" + "".join(
            f"#define {key} {json.dumps(data[field])}\n"
            for key, field in (("XV_BUILD_VERSION", "version"), ("XV_BUILD_REVISION", "revision")))
        a.output.parent.mkdir(parents=True, exist_ok=True)
        if not a.output.exists() or a.output.read_text() != text:
            a.output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    main()
