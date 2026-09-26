#!/usr/bin/env python3
"""Attribute Halo CE 54010's indirect callbacks with XV_SCENE_PHASES timers.

Only modifies generated private stage code. Captures each target before the
guest return-address push, as the original does, and retains xv_call dispatch.
Runtime timing remains disabled unless XV_SCENE_PHASES is enabled.
"""
import argparse
from pathlib import Path
import re

RETURNS = {"540E3", "541C4", "54221", "54257"}
MARK = "/* xita timed scene callback */"
CALL = re.compile(
    r"(?P<indent>^[ \t]*)\{ uint32_t t_ = (?P<target>[^;\n]+); "
    r"X_PUSH32\(0x(?P<ret>[0-9A-F]+)u\); xv_call\(c, t_\); \}", re.M)


def instrument(source):
    start = re.search(r"^void f_00054010\(xctx \*restrict c\)\n\{", source, re.M)
    if not start:
        raise ValueError("missing expected 54010 definition")
    end = source.find("\nvoid f_", start.end())
    if end < 0:
        end = len(source)
    body = source[start.start():end]
    if MARK in body:
        if body.count(MARK) != 4 or CALL.search(body):
            raise ValueError("partial callback instrumentation; refusing to modify")
        return source
    calls = list(CALL.finditer(body))
    if len(calls) != 4 or {m["ret"] for m in calls} != RETURNS:
        raise ValueError("callback sites differ from audited CE layout")

    def wrap(m):
        i = m["indent"]
        return (f'{i}{{ {MARK}\n'
                f'{i}    uint32_t t_ = {m["target"]};\n'
                f'{i}    X_PUSH32(0x{m["ret"]}u);\n'
                f'{i}    extern void xv_scene_phase_begin(uint32_t);\n'
                f'{i}    extern void xv_scene_phase_end(uint32_t);\n'
                f'{i}    xv_scene_phase_begin(t_);\n'
                f'{i}    xv_call(c, t_);\n'
                f'{i}    xv_scene_phase_end(t_);\n'
                f'{i}}}')

    return source[:start.start()] + CALL.sub(wrap, body) + source[end:]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("shard", type=Path)
    args = parser.parse_args()
    old = args.shard.read_text()
    new = instrument(old)
    if old != new:
        args.shard.write_text(new)
    print("54010: four callback timers " + ("installed" if new != old else "already present"))


if __name__ == "__main__":
    main()
