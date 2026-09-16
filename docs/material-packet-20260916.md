# Material packet prototype: keep disabled

This isolated prototype replaces five exact state-only islands in Halo 3925's
`70110` with a native helper. Production helper and emitted-hook differential
checks pass, but the compiled ARM instruction reduction is too small to justify
prioritizing deployment. No physical performance improvement is established.

The ordinary build has no packet hook calls. Opt in explicitly with
`make RECOMP=1 XV_MATERIAL_PACKET=1`; the runtime environment variable of the
same name still defaults OFF. The Makefile tracks the mode for the helper and
generated units containing the hook, including when reusing a build directory.
Regenerate guest code through the Halo adapter before evaluating this option.

## Evidence and sizing

The parent physical capture used runtime
`a360eda30593ef422c03096cb1ad0f2b95e7da13d8e8a74146cef24e5b22462f`.
It attributed 11.279 ms/frame to `70110`, with 62 entries/frame. That routine
also invokes drawing through `7A960`; its selected self has no selected child
subtraction. Ordinary draw-HLE time around 13.3 ms/frame already overlaps it.
The five setter islands are only a small part of this envelope. Detailed phase
instrumentation adds about 23 ms/frame. None of these numbers is a packet saving.

Separate translation units compiled by VitaSDK GCC for Cortex-A9, Thumb,
NEON and `-O2 -fno-strict-aliasing` produce these executed instruction counts:

| Island | Original mean | Native mean | Runtime OFF overhead | Compiled out overhead |
| --- | ---: | ---: | ---: | ---: |
| `7036D` common state | 836.94 | 731 | 124 | 0 |
| `70440` stage 0 | 407 | 395 | 108 | 0 |
| `70498` stage 1 | 407 | 395 | 108 | 0 |
| `704FF` stage 2 | 407 | 395 | 108 | 0 |
| `70566` stage 3 | 486 | 446 | 108 | 0 |

The full five-island path saves about 182 instructions, about 7% of these tiny
islands, and at most roughly 11,300 instructions/frame if all 62 entries take
this path. Some material modes bypass it. The runtime-OFF hook adds about 556
instructions per full path. **These are instructions, not cycles or FPS.** The
test worker predicate is a flag; the real native thread identity query is
excluded and may eliminate the small gain. First-use initialization contributes
only to the first active comparison. Default compiled-out spans have identical
instruction counts and architectural results to the original spans.

## Exact boundaries and state

The adapter checks both the complete supported image and each local byte span.
Each hook branches to an independently named resume label only on success;
every original instruction remains as fallback. Entry/resume pairs are:

| Entry | Resume, exclusive | Original HLE calls |
| --- | --- | ---: |
| `7036D` | `70433` | 8 |
| `70440` | `70486` | 5 |
| `70498` | `704ED` | 5 |
| `704FF` | `70554` | 5 |
| `70566` | `705CC` | 6 |

Common-state inputs include BL, EBP's material flag byte, ESI and ESP. It applies
the production seven `rs_method` transitions and cull semantics, preserving
ordered image mirrors at `18F48C/46C/478/47C/4A8/470/474`. The material flag is
read after earlier stores, matching mapped aliases. Exit ECX is `40340`, EDX
`7F`, EDI the alpha-test value and ESP entry minus 12, with the next texture
callback's three arguments pushed. Lazy flag operands, width and overrides are
preserved, including the old EDI operand of the disabled-alpha XOR.

Texture islands update the existing deferred texture table in order. Stages
0/1/2 write states 10/11 as 1 and 13/14/15 as 2. Stage 3 writes 10/11/12 as 3
and 13/14/15 as 2. Exit ECX is the stage and EDX is 15; ESP and all other context
bytes, including flags, retain their original result. Every intermediate guest
scratch write is preserved, not only the final scratch values. This permits
the guest stack, material bytes, image mirrors and texture table to alias.

The four `80360` calls at `7043B/70493/704FA/70561` remain in place. Their
animated/fallback resource resolution, binding, shared-resource transactions
and dimension updates remain unchanged. No preemption point, draw, GXM command,
retained resource, GPU lifetime or callback is moved. There is no persistent
material snapshot or mutable-guest zero-copy path.

## Admission and controls

With the experiment compiled, marked object jobs and actual native object
worker threads decline before owner configuration or counters are accessed.
Under `XV_EXPERIMENTAL_OBJECT_JOBS`, absence of the weak
`xv_object_is_worker_thread()` dependency also declines. This shared API is
provided by the separate object-worker change; this patch does not redefine or
edit `xk_object_jobs.c/h`. A false worker check is not proof of owner identity.
The generated render path and all control/report APIs require the recording
owner. They are not general cross-thread interfaces.

Diagnostic builds/modes requiring original HLE observations decline: guest
trace, function trace, watchpoints, on-demand D3D histogram, and startup presence
of `XV_D3D_HIST`, `XV_DS_CHECK`, `XV_LOG_RS`, `XV_WATCH_FN` or `XV_PROF`.
Startup environment configuration is cached once. A decline changes no guest
context, guest memory or D3D state. Eligible runtime-OFF calls increment only
the bounded owner eligibility counters.

`xv_material_packet_override(-1/0/1)` restores configured policy or selects an
arm. `xv_material_packet_available()` reports runtime compatibility, not proof
of generated image/span acceptance. Both require the existing drained owner.
The benchmark must see nonzero accepted counts before claiming coverage.
`xv_material_packet_read_counters()` snapshots/resets five saturating eligibility
and acceptance counters. The existing post-Present 60-frame owner report emits
and resets them after object jobs have joined. There are no per-call clocks or
logs. Compiled-out builds return unavailable and emit no packet report line.
No benchmark enum, remote command or graphics setting is included in this patch.

## Validation

Run with the local supported XBE, manifest and symbol file, plus `iced-x86`:

```
python tools/test_material_packet.py --output-dir recomp/host/build/material-packet
python tools/test_arm_material_packet.py --output-dir recomp/host/build/material-packet-arm
python tools/test_game_profiles.py
```

The ARM fixture additionally needs VitaSDK GCC, `unicorn` and `pyelftools`.
Original spans are generated privately from the local image. No game bytes or
generated game instruction bodies are committed.

Actual results in the isolated worktree:

- Six ASan/UBSan runs of 2,560 helper/hook comparisons pass, covering serial and
  object-worker builds with configured unset/0/1, explicit override and restore.
  Each compares the complete `xctx`, 2 MiB guest arena and production D3D state.
- Unaligned and page-crossing stacks, mapped material/image/table aliases, all
  BL values, varied flags/registers/SIMD/MMX/x87 state, worker marker, an unmarked
  context on a native pthread, unavailable worker dependency, diagnostics,
  saturation, report reset, invalid IDs and null-context decline pass.
- Compiled-out hooks and runtime-OFF hooks preserve the original result.
  A changed complete image and 15 individual span mutations suppress their
  hooks. Stripping only hook/label blocks leaves the entire emitted `70110`
  byte-for-byte unchanged. Trace emission retains ordinary HLE observations.
- 640 Cortex-A9 instruction-level comparisons pass against production
  `xd3d.c` plus generated original spans in separate translation units. Five
  additional compiled-out comparisons have exactly equal instruction counts.
- All 10 synthetic game-profile tests and `git diff --check` pass. Both
  Makefile mode recipes were checked. No full game/package build was performed.

The helper fixture supplies the worker identity predicate using a test flag;
it does not validate the OS predicate itself. That predicate is independently
covered by the object-worker change. No hardware, Vita3K, remote benchmark,
deployment, or authoritative source/stage mutation was performed.

## Larger follow-up: ordered scene traversal

`54010` has 10.058 ms/frame selected self at 10 entries/frame in the same
capture, but its four indirect callback sites are not separately attributed.
It walks scene `39BE58`, 32-byte material groups, 256-byte geometry entries and
ordered visible-index runs. Before callback `540E1`, regular draw `541C0`,
alternate draw `5421D`, and after callback `54255` are correctness boundaries.
Backedges at `5413F`, `54247` and `5426B` are scheduler boundaries.

The concrete larger candidate is an owner-only native traversal of
`54050` through `5426B`, retaining the callback calls and guest-visible state at
each boundary. Locals can avoid repeated register/flag traffic between these
boundaries; every callback and scheduler handoff must materialize live guest
state and reload mutable records afterwards. Do not precompute or cache a
whole callback list across shared-state mutations, nor reorder transparent work.
Admission and span guards would remain strict, with ordinary fallback.

First attribute elapsed time and existing draw-HLE counter deltas around the
four callback sites, keyed by a bounded target set. Caller examples establish
real hidden draw work: `5D72A` supplies `62760/622A0/61E40`, `545CB/545F8`
supply `624B0` (which calls `7A3D0`), and `544B5` supplies `62880` (which
calls `6AB50`). `62870` and `627E0` also contain texture/constant setup and
draw descendants. Separate callback cost from traversal before investing;
10.058 ms is not an available native-traversal saving. The same existing
draw-counter subtraction at `70110` entry/exit would size its non-draw remainder
without new per-draw clocks. Final frame comparisons must disable detailed
phase tracing and preserve scheduler/graphics settings.

## Integration conflicts

This patch is based on `f3ad924147ed3ec3ecc2a5468d2a86217694920d` and changes
`games/halo_ce_3925/hooks.py` beside other native-hook work, plus the optional
Makefile mode block. Reconcile those hunks while preserving each independent
hook. It does not touch runtime benchmark/remote files or object-worker files.
The worker predicate change must accompany an object-jobs-enabled experiment;
otherwise availability correctly remains false. Given the negative sizing,
deferring this patch and its hardware evaluation is reasonable.
