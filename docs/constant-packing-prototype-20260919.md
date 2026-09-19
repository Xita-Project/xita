# Native model constant packing: first ARM experiment

The private retained-function experiment for Halo CE 3925 routine `7E530`
passed 126 comparisons against the original translated function. This routine
packs scaled model matrices into vertex shader constants; it is downstream of
the model packet work reached from `5B760`. This is not a BSP traversal replacement.

The prototype packs the first N-1 matrices with native ordered double products
and float stores, then executes the original final iteration and constant
submission. It preserves the skipped loop's preemption budget consumption and
falls back when that budget would permit a handoff.

The comparisons cover complete guest context, all 8 MiB of fixture guest RAM,
and final ARM FPSCR. Cases vary matrix count (0, 1, 2, 4, 16, 32, 64), identity
and nontrivial finite inputs, three x87 stack positions, and three FPSCR values.
The submission is a fixture stub; the retained function's phase observer is
disabled equally in both builds.

| Matrices | Original ARM instructions | Candidate ARM instructions |
| --- | ---: | ---: |
| 1 | 675 | 686 |
| 2 | 1,230 | 901 |
| 4 | 2,334 | 1,263 |
| 16 | 8,958 | 3,435 |
| 32 | 17,790 | 6,331 |
| 64 | 35,454 | 12,123 |

These are Unicorn instruction counts, not hardware cycles or frame-rate gains.
The result justifies hardening this candidate rather than discarding it as a
minor loop-wrapper change. The single-matrix case incurs a small admission cost.

## Remaining work before deployment

- Replace fixture-specific address admission with validated contiguous mapping,
  host alias and descriptor/stack exclusion checks.
- Exercise nonfinite/denormal inputs, additional rounding modes, preemption
  frontiers, count changes, and rejected/overlapping mappings.
- Confirm calling-thread ownership of the global constant destination and
  preserve the real HLE submission path.
- Add bounded admission/size counters to establish how often gameplay uses it.
- Compare the cumulative build in the same hardware scene; no FPS benefit has
  yet been measured for this candidate.

No gameplay integration or hardware deployment was performed by this experiment.
Private reproduction artifacts (including retained generated game code, excluded
from Git) are in `../constant-pack-prototype/`: `create.py`, `run.py`, `results.json`,
and `run.log` relative to this checkout.

## Guarded contiguous-pointer follow-up

The candidate now lives in `recomp/kernel/xk_constant_pack.c`, behind the
unconfigured `XV_NATIVE_CONSTANT_PACK` compile gate. It validates contiguous
spans, rejects host-memory aliases between output and source/descriptor/saved
stack/context, and requires enough preemption budget for the entire prefix.
It uses the validated pointers for the inner loop, eliminating repeated guest
address lookups while retaining ordered double multiplication and float stores.

The 126 original comparisons and 289 additional cases passed (415 total).
Additional cases exercise signed zero, subnormal/extreme finite floats,
infinities, quiet/signaling NaNs, five FPSCR configurations, budget thresholds,
input/output and output/stack overlap, host-page aliases, discontiguous input,
negative counts, and an oversized count. These compare final state, not a
production HLE execution or a hardware scheduling trace.

With guards and direct pointers, the representative instruction counts are:

| Matrices | Original | Guarded candidate |
| --- | ---: | ---: |
| 1 | 675 | 725 |
| 2 | 1,230 | 888 |
| 4 | 2,334 | 999 |
| 16 | 8,958 | 1,659 |
| 32 | 17,790 | 2,539 |
| 64 | 35,454 | 4,299 |

This supersedes the initial prototype cost table. Runtime integration still
needs owner-thread gating and confirmation that direct writes respect any
active guest memory write-tracking policy. Admission counters and a strict
profile-specific hook remain outstanding. No hardware update has been made.

## Runtime hook and ownership gate

Added a strict emitted-body hook in `games/halo_ce_3925/constant_pack.py`, applied
only by the audited CE profile. It runs on initial loop entry, retaining the
original final iteration, backedges and submission. Makefile opt-in requires
CE, owner phase tracking and the existing object-job backend; default builds
leave it disabled.

The helper now requires the active presenting-owner scene context and a drained
object-job interval. The new read-only idle query relies on that independently
verified owner identity. It adds no locks or worker policy changes. Checked
memory builds mark destination write epochs through `X_GWN`; an active diagnostic
write watch declines to the original path. The checked helper compiled with VitaSDK.

The actual strict hook passed the original 415 final-state comparisons plus two
owner/active-worker rejection cases, 417 total. This fixture stubs the ownership
query; it does not establish scheduling safety of the complete application.
A full linked build, diagnostic write-epoch runtime test, usage reporting and
physical hardware measurements remain outstanding. The installed Vita build
remains perf.29.

## Checked-memory and reporting follow-up

The full 417-case suite also passed with `XV_CHECK_GUEST_ADDRESS`, comparing the
complete page-epoch array in addition to guest memory/context/FPSCR. Fresh-epoch
cases at 2, 16 and 64 matrices verify all destination pages receive the current
epoch; an enabled write-watch case verifies fallback equivalence. Policy and
watch callbacks are fixture implementations, not the production memory subsystem.

Added owner-only per-report counters for accepted batches, packed prefix matrices,
and rejection reasons. Linked CE builds opt in explicitly; the new compile flag
is restricted to the hooked guest unit, packing helper and reporting unit.
