# Repeated model fog arithmetic in campaign

The model preparation audit identified repeated arithmetic within primary
`70110:70A42–70D07`. World and first-person model parts can enter that block
with the same depth, camera, fog plane and fog colors. Material/mesh data is
not an input to this block. Reusing its exact result is distinct from the earlier
whole-material fusion, which repeated the arithmetic and added state-copy costs.

## Physical input census

A private observer build retained every perf.5 optimization and always executed
the original fog code. Its runtime SHA-256 was
`8dd5625d49de6a584232b340c3b404a850638c751a7fca5ae2eb194d2d8e8ce8`.
The updater verified its boot in slot 0. Ordinary menus restored New001's
Pillar of Autumn checkpoint; a screenshot and save-file reads confirm gameplay.
No built-in benchmark ran.

The campaign interval covers 92,160 fog entries, with 67,424 consecutive exact
key matches: **73.16%**. Surrounding 60-frame report anchors imply about
31 entries per frame; the anchors are not exact census endpoints. That modest
call volume makes this a cumulative optimization candidate, not a proposed
solution to the whole gap between approximately 13 and 20 FPS.

The key checked all 19 raw float inputs, the image flag, ESP, TOP, FSW, FCW,
FPSCR excluding overwritten NZCV, root identities and stack mapping. This
is an upper bound on production eligibility: the observer did not enforce
the production owner and input/stack alias checks. It also exposed a mismatch
in the first prototype's admission policy: actual FCW was `023f`, with native
FZ/DN and sticky flags such as FPSCR `63000090` through `6300009f`.
The old control guard therefore reported zero qualified calls, despite no
numeric or single-page-stack declines. The implementation was separately
qualified for the observed control states before enabling them.

The observer includes extra lookup/logging work. Its FPS is not an optimization
result. After collection, perf.5's actual runtime digest was verified again:
`8431cfb669e360a9e36d798539456d5696e7013f8836572393cc2c6b23b831cf`.
Private counters, captures and restore receipts are under `fog-census/` in the
unified-games workspace.

## Exact replay contract

The optional `XV_MODEL_FOG=1` build requires CE and the owner observer. Its
image/body-pinned generator hooks only primary `70110`; generic/interior entries
remain original. A miss executes the original inline region and records its
outputs. A hit resumes at the original publication continuation. Runtime
disable, unsupported FP/inputs, aliases, ownership/root changes and trace/watch
diagnostics retain original execution and invalidate reuse as appropriate.

Replay preserves the full qualified context and final guest writes, including
six registers, lazy flags, five physical x87 slots, FSW and native FPSCR.
The exact stack footprint is 80 bytes. The child's saved EBX word is rebuilt
from the current invocation, not copied from the previous call. Shader and
texture selection, constant/combiner publication and draw order are retained.

The final production helper's retained-ARM fixture models 2,992 instructions
for the original ordinary region, 4,361 for an initialized miss, and 1,682 for
a hit. A miss also imports 264 firmware-copy bytes. The instruction-only
break-even is about 51% hits, before real ownership-query and firmware-copy
costs. These numbers replace the earlier, cheaper prototype's estimates; they
are not Vita cycles or an FPS prediction.

Hardware admission counts, complete-frame timing and rendering still need
verification in the cumulative candidate. The larger concurrent target is
[retained CPU vertex snapshots](retained-vertex-capture-20260918.md).
