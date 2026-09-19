# Exact UV reuse within a model render pass

`XV_MODEL_UV=1` optionally reuses the common material UV calculation within one
selected model pass. It defaults Off, requires the CE 3925 profile and the owner
phase support, and preserves every original cold calculation and generic entry.
No shader approximation, draw sorting, resource lifetime extension or worker
ownership change is introduced.

The two selected calls from primary `70110` to `56F20` run inside an explicit
`A26B0 → A2380` scope. The cache is invalidated on scope exit, unsupported input,
diagnostics or nesting; nested work blocks further reuse in the outer scope.
Foreign workers, native threads, contexts and fibers are rejected before touching
mutable cache state. The owner generation, active scene, arena/image/page roots,
image bounds and current model packet must match. Packet addresses alone are not
lifetime tokens.

Each attempt reads the current canonical UV descriptor, six raw argument words,
image constants, floating-point stack position and supported FP control/status.
Physical span checks reject misalignment, page seams and aliases among outputs,
scratch and inputs. Cache hits reproduce the two rows, original scratch/argument
writes, overwritten x87 slots, live registers, lazy flags and final FP status.
The public routine and all declined calls retain the original code. In particular,
the original unrounded double cosine and float-rounded sine inputs remain exact;
a default 360-degree rotation is not replaced with an identity matrix.

Only incoming guest FSW bits `0x4700` and native FPSCR bits `0xf0000000` are
omitted from key equality. The first original comparison overwrites those bits
before their first read. Source and retained ARM disassembly confirm this.
All admitted cumulative exception flags, rounding/FZ/DN settings, remaining FSW
bits, TOP and FCW remain keyed. Unsupported states, including native QC, decline.
Replay does not normalize, erase or guess outgoing status bits.

## Qualification

- 109 comparisons of the production helper/lifecycle against full original
  `56F20`, waveform-zero code and linked Vita math library. Complete context,
  8 MiB arena and raw FPSCR match. The real owner-phase predicate is linked;
  OS thread/fiber identity and worker identity are controlled fixture inputs.
- 24 actual generated caller-region comparisons, including production shader
  constant publication, entry snapshots and complete D3D state. Callback changes
  to live context, scale and time retain exact behavior.
- 2,374 independent production-key comparisons. These include all 16 guest
  condition-bit combinations crossed with all 16 native condition nibbles at
  every x87 TOP, all rounding/FZ/DN combinations in additional cases, and retained
  live-bit misses/unsupported-QC decline. Full context, arena and raw FPSCR match.
- Full primary `A26B0` and `70110` compile Off with original text/relocation
  identity. Six real Make/archive transitions verify selective rebuilding and
  removal of the helper when disabled. Invalid selections and missing hooks fail.
- 864 sequential caller comparisons carry every prior x87 slot, status/control
  field and raw native FPSCR into the next invocation. They cover front-only,
  back-only and alternating sequences, every TOP and six initial FPSCR states.
  Full context, arena, production constant state and publication-entry snapshots
  remain equal. Next-call GPR/argument setup is synthetic; intervening full
  material/model code is not executed.

The fixtures do not execute a complete model walk, real concurrent scheduler or
GPU draw. Isolated hit fixtures reseed FP entry state; the sequential fixture
carries it forward, but neither measures naturally occurring campaign hits.
Private evidence is under
`model-uv-reuse-prototype/production/` and
`model-uv-normalized-key/production-final/` in the unified-games workspace.

Reproduce with owned inputs and VitaSDK/Unicorn/pyelftools:

```sh
python tools/test_model_uv.py --xbe "$XBE" --manifest "$MANIFEST" \
  --retained-build "$RETAINED_BUILD" --out "$PRIVATE_EVIDENCE"
python tools/test_model_uv_caller.py --xbe "$XBE" --manifest "$MANIFEST" \
  --retained-build "$RETAINED_BUILD" --out "$PRIVATE_EVIDENCE"
python tools/test_model_uv_key.py --elf "$PRIVATE_EVIDENCE/uv.elf" \
  --output-dir "$PRIVATE_KEY_EVIDENCE"
python tools/test_model_uv_sequence.py --elf "$PRIVATE_EVIDENCE/caller.elf" \
  --output-dir "$PRIVATE_SEQUENCE_EVIDENCE"
python tools/test_model_uv_build.py --out "$NEW_PRIVATE_BUILD_EVIDENCE"
```

Keep generated game bodies and linked fixtures outside the repository.

## Cost and hardware acceptance

The bounded production path uses 1,946 modeled ARM instructions originally,
3,298 on a cold cache call and 1,084 on a hit. That needs approximately 61.1%
eligible hits to break even in this model, before model-scope setup and real
OS/worker identity lookup costs. Instruction counts are not hardware cycles or
an FPS prediction. No physical speed improvement has been established.

Sequential callers with initially clear cumulative status execute two cold UV
calculations before reuse: the first original call sets native inexact status,
which correctly changes the next key. With inexact already set, only the first
call is cold. For six repeated front callers, the instruction model gives
14,822 original versus 14,029 candidate with clear flags (14,866 original for
the zero-FPSCR case), or 14,822 versus 11,679 with inexact already set. A short
scope may cost more than it saves; these totals exclude model-scope setup and
real identity lookup costs. Do not interpret six-call savings as an FPS gain.

Joined `[model-uv]` counters distinguish scope and first/second-side volumes,
hits, cold calls, argument-only/FP-only/mixed misses, changed FP fields and
admission declines. There are no per-call timers. The next hardware check must
confirm useful live reuse and frame behavior after a fresh launch with the
existing cumulative options retained. Descriptor similarity alone is not proof
that the optimization helps.

## Candidate package

`0.2.0-perf.15` / `3b5d14a+` builds successfully with UV reuse enabled and the
perf14 cumulative options retained. All 1,744 VPK entries verify; only
`game-a.self` and `boot-game.txt` differ from perf14. Runtime SHA-256 is
`f62a19727f18c88d3dda2135ede657113226c91a8ef513745453ae9ed6d5372d`.
After the remote service became reachable, the updater verified this runtime and
boot-confirmed it in physical slot 0. The dashboard and gameplay overlay both
show `0.2.0-perf.15 / 3b5d14a+`. Perf14 remains in the other slot for rollback.
The user's earlier faster campaign-loading observation has no measured loading
comparison and predates this candidate's installation.

## First physical campaign result

The Normal/New001 marine checkpoint loads and renders on perf15. The stationary
camera matches the earlier checkpoint (`-28.66, 32.52, 0.62`, forward
`0.56, 0.82, -0.15`). No benchmark mode was used and the saved graphics settings
were retained. The last twelve complete initial windows have medians of
78.35 ms/frame, 12.75 FPS and 153 draws/frame. Perf14's earlier initial capture
was about 78.3 ms, 12.75 FPS and 149 draws/frame. These are ordinary runs with
varying NPC activity, not a controlled paired comparison. No clear overall FPS
gain is established.

Those twelve UV rows contain 13,021 hits, 7,991 cold calls and 22,397 total
calls: 61.97% hits among eligible calls, 58.14% of all calls. Normal steady rows
show no argument/FP misses; repeated scope initialization and unsupported
programs still cause cold work. Typical rows contain 540–650 completed scopes
per 60 frames, 60 root declines and 60–115 program declines. Scope checks remain
part of the cost; this observed hit ratio is close to the isolated instruction
break-even estimate, so repeated-computation savings are not automatically net
savings.

Remote camera inputs subsequently changed the recorded forward vector and the
scene continued rendering. Trigger input was also submitted; this limited smoke
test does not establish extended combat stability. Captured initial and
after-input logs contain no searched STOP/FATAL/GPU-crash/trap marker. Their
private evidence and summaries are in `ce-perf15/gameplay/`.

Before deployment, the saved perf14 history was recovered: one complete log
contains 1,226 sixty-frame reports and runs to approximately 5,591 seconds. No
searched fatal/GPU-crash marker appears there either. Its final, different view
has about 201 draws/frame and 12 FPS; do not use it as the UV checkpoint baseline
or infer that an abrupt log ending proves a clean exit.
