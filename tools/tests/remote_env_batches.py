"""Run directly with Python; no Vita or credentials required."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from vita_remote import env_batches

for size in (510, 511):
    pair = "A=" + "x" * (size - 2)
    assert env_batches([pair]) == [pair]
pairs = ["A=" + "x" * 250, "B=" + "y" * 250, "XV_FRAME_TIMES=1"]
batches = env_batches(pairs)
assert len(batches) == 2
assert all(len(b) <= 511 for b in batches)
assert "&".join(batches) == "&".join(pairs)
for bad in ("A=" + "x" * 510, "A=x&B=y", "1A=x", "A=hello world", "A=é"):
    try:
        env_batches(["GOOD=1", bad])
    except ValueError:
        pass
    else:
        raise AssertionError(bad)
print("PASS: query limits, ordered batching, trailing option retained, malformed input rejected")
