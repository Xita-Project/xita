# Contributing to Xita

[README](README.md) · [Roadmap](ROADMAP.md) · [Compatibility](COMPATIBILITY.md)

The repository is being prepared for a future public release. These requirements
apply to proposed changes; they do not authorize publication of the current
development history or game-derived build products.

## Before opening a PR

Keep each PR focused on one problem. Explain the behavior before and after the
change, how to reproduce it, and any compatibility or performance tradeoffs.
For a new title or substantial architecture change, start with a design issue
so the required runtime and shader work can be scoped.

## Source and license requirements

- Original contributions must be compatible with **GPL-3.0-only**. Confirm that
  you own the contribution or have permission to submit it under compatible terms.
- Identify copied or adapted code, its source revision, license and required
  notices. Update [THIRD_PARTY.md](THIRD_PARTY.md) when appropriate. Do not remove
  existing attribution or apply Xita's license to someone else's assets.
- Do not include game executables, maps, translated game code/shaders, captured
  shader definitions, SDK/firmware binaries, private saves, secrets or raw dumps.
  Hashes and regeneration tools are preferable to embedding original game bytes.
- If AI tools assisted, say what they were used for. The contributor remains
  responsible for understanding, testing and checking provenance of the result.
- Inspect the staged diff. `.gitignore` does not protect files already tracked.
  Run the [release audit](docs/release-audit.md) before proposing distributable files.

## Validation requirements

Run checks relevant to the change and include their results. Documentation-only
changes need link/command review, not invented runtime tests. Runtime changes
need regression coverage for the behavior or failure mode they affect.

- Label **Vita hardware**, **Vita3K** and **host tests** separately. Mark tests
  not run and explain any limit; never imply emulator success clears a hardware crash.
- For performance claims, record the build, map, camera/route, resolution,
  graphics settings, frame cap, clocks and measurement method. Prefer matched
  before/after/before samples. Do not report a peak as an average or combine
  overlapping CPU/GPU timers into an execution-time total.
- Rendering changes should include relevant comparisons when those captures
  can be shared with permission. Keep unreviewed captures and raw diagnostics private.
- Preserve GPU ownership until completion. Do not reuse in-flight buffers or
  bypass retirement to improve a timing number. Retain a tested fallback for
  experimental behavior.
- Guard title-specific hooks by the supported executable/version. Do not change
  simulation speed, AI, physics or gameplay semantics to inflate FPS silently.

## PR contents and review

Use the PR template. Include the problem, resulting behavior, validation and
provenance. Update user documentation or compatibility records when behavior
changes, with evidence and dates. Avoid unrelated formatting or generated output.

A maintainer reviews the change before merge. Public release additionally needs
the provenance and clean-build gates in [the release audit](docs/release-audit.md).
Passing a test or the automated audit alone is not legal clearance.
