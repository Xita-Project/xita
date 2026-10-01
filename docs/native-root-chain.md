# Native root-matrix preparation candidate

The current candidate joins the three matrix operations at Xbox 3925
`8E419..8E58B` into one native call for unattached objects. It retains the
existing arithmetic, including intermediate float rounding, and the basis
matrix write. This is one boundary within `8DDF0`, not a replacement for the
whole object-update system.

`XV_ROOT_CHAIN=1` enables it; absent/zero keeps the original path. The hook is
covered by the existing exact-image hierarchy signatures. Scene diagnostics,
disabled native math, optional NEON matrix policy, attached objects, unsupported
layouts and aliasing keep the existing implementation. The current deployed
perf314 does not contain this candidate.

## Contract

Entry is the pending matrix call with return address `8E41E`. Its arguments
must be the expected stack-relative basis, offset and destination. The parent
matrix pointer must be zero. The captured stack span must be aligned and
physically contiguous across any guest-page boundary. The output matrix must
be disjoint from the entire captured stack span; physical aliases are rejected.
No guest state changes before admission.

The helper captures basis, offset, world translation and local orientation.
It performs `(basis * offset)`, then world translation with that result, then
local orientation with the second result, using the same matrix helper and
operand order as the current runtime. It retains the updated basis, final
matrix, final stack footprint, registers, lazy flags, x87 slot and SSE lanes.
No simulation tick, node or object update is skipped. Inputs are captured before
the existing private-math window; guard and publication rules are retained.

## Evidence so far

Private evidence directory: `../root-chain-315/` relative to the source checkout.

- Host ASan/UBSan: 1,024 full-context/arena/FP cases pass; four disabled or
  diagnostic modes each pass 1,024 mutation-free declines; 15 malformed-layout,
  alias, alignment, page and address-range declines pass.
- Pi ARM32 Thumb/Cortex-A9 build: the same cases pass, comparing the complete
  FPSCR with all four rounding modes and FZ/DN combinations, including signed
  zeros, denormals, infinities and NaNs.
- Exact-image hook test: passes with both actual insertion sites, three changed
  image guards, and disabled preprocessor output identical to the prior path.
- Complete lifted hierarchy on host: 254 comparisons per mode, including 32
  new non-world-relative root cases with mirrored/unmirrored objects. Native
  root-chain uptake is confirmed, rather than inferred from passing fallbacks.
- Complete lifted hierarchy on Pi: enabled and native-math-disabled modes pass
  the same 254 comparisons; enabled reports 32 root-chain admissions.

The first integration run admitted zero roots because the synthetic caller's
stack crossed a page boundary. Admission was corrected to prove physical
contiguity across pages. It still rejects non-contiguous mappings.

The final Pi microbenchmark against the already-fused root-pair path measured
roughly 0.470 versus 0.372 microseconds/call across three trials per path.
This only supports reduced local overhead. It is not a prediction of Vita FPS
or evidence of a substantial whole-frame gain. Hardware qualification remains
required; Silent Cartographer's last accepted diagnostic measurement was still
about 124 ms/frame, well above the 50 ms target.

## Integration and next step

The private perf315 stage is based on perf314. Its audit retains all guest
instructions and existing x87 spills, adds two hooks in the register body and
two in the memory fallback, and reloads x87 locals before the successful jump.
It retains earlier stage-only hierarchy fusion and other accumulated changes.
The source generator remains conservative about non-observer body rewrites.

The Vita build and package audit pass: only the math object, generated shard
and four version objects changed; the updater asset contract is unchanged.
The candidate is not deployed. Next test uptake and ordinary b30 gameplay
with a cold restart and the strict object-worker gate. Keep the same settings
and isolated save namespace. Report whole-frame times and stalls; do not infer
an FPS win from helper counters or Pi timings. Broader animation and character
update restructuring is still needed.

## Physical Vita perf315 result

The 600-second capture completed, including loading and stationary b30 gameplay.
The expected runtime hash booted; `[root-chain]` confirms tens of thousands of
admissions per 60 frames (last report 38,880 accepted / 3,840 declined). The
beach, weapon and nearby actors rendered in the captured screenshot. This is
not a 15-minute active combat or full correctness qualification.

The last 600 complete present intervals (report ends 7260–7800) average
124.97 ms (8.00 FPS), p95 136.10 ms, p99 284.25 ms, maximum 565.81 ms.
All 600 exceed 50 ms; ten exceed 200 ms. Perf314's corresponding late sample
was 124.28 ms. Different actor states prevent treating this as a controlled
regression comparison, but there is **no demonstrated whole-frame gain**.
Pi microbenchmark improvement did not establish hardware frame improvement.
Keep the candidate opt-in, not a claimed speedup; dashboard restoration omits
XV_ROOT_CHAIN so the next ordinary launch uses its default-off policy.
Private evidence: `../root-chain-315/{b30-stream.log,arrival.png,
settled-frame-summary.json,lifecycle-summary.json,object-pass-patch-check.json}`.
