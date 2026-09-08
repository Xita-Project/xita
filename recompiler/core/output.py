"""Keep failed game postprocessing and stale generated chunks out of builds."""
import json
import os
from pathlib import Path
import re
import tempfile

CHUNK = re.compile(r"code_[0-9]+\.c\Z")


def check_output_identity(directory, profile_id, input_sha256):
    report = Path(directory) / "recomp_report.json"
    if not report.exists():
        return
    previous = json.loads(report.read_text())
    # Pre-profile development outputs have no identity record. They can migrate
    # once; all newly emitted directories are then pinned to one game/revision.
    if "input_sha256" in previous and (previous["input_sha256"] != input_sha256 or
                                      previous.get("profile") != profile_id):
        raise ValueError("Output belongs to another profile/revision; choose a separate --outdir")


def emit_output(emitter, hooks):
    destination = Path(emitter.outdir)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".xita-recomp-", dir=destination.parent) as temp:
        old_outdir = emitter.outdir
        emitter.outdir = temp
        try:
            count = emitter.write_all()
            hooks.postprocess(temp)
        finally:
            emitter.outdir = old_outdir
        # No existing source is changed until all profile postprocessing passes.
        generated = list(Path(temp).iterdir())
        destination.mkdir(exist_ok=True)
        current = {p.name for p in generated}
        for source in generated:
            os.replace(source, destination / source.name)
        for stale in destination.glob("code_*.c"):
            if CHUNK.fullmatch(stale.name) and stale.name not in current:
                stale.unlink()
        return count
