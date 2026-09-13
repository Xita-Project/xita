# Model matrix batching experiment — September 13, 2026

The [guest workload audit](guest-workload-audit-20260912.md) identified independent
matrix products in Halo CE 3925's model renderer. An experimental native batch now
replaces that loop when its safety checks pass. It reduces repeated guest-call
and register-state work; it does not introduce another gameplay worker or
establish a Vita FPS gain.

## Scope and fallback

The game profile verifies the executable, the complete `0xA2781..0xA27C5` loop
and its `0xB5B40` matrix callee before emitting the optional hook. Ordinary builds
omit the helper. Experimental builds require `XV_NATIVE_MODEL_PALETTE=1` both
when building and in the runtime configuration. `XV_NATIVE_MATH=0` disables it.
Use a separate build directory when changing compile-time options.

For each node, a 52-byte pose multiplies the inverse-bind matrix in the 156-byte
model-node record. The batch preserves the existing ordered float operations,
double scale calculation, matrix outputs, final guest registers, x87/SSE state,
flags, guest-call scratch and scheduler budget. Only the final matrix updates
the guest register state; no scheduler handoff occurs inside an accepted batch.

The original loop remains intact. It handles unsupported counts, misalignment,
wrapping addresses, noncontiguous mapped pages, output/input aliases (including
physical aliases), and budgets that would yield during the original loop. Every
guard runs before the first guest mutation. Input arrays may alias each other.
The native object uses `-ffp-contract=off`; reassociation or fused products would
change the reference arithmetic.

## Validation

`tools/test_model_palette.py` independently lifts the original loop and matrix
routine from the developer's own executable. Its host harness compares the full
context and a 1 MiB guest arena against both the original lift and the current
per-matrix native path. It covers 4,096 arithmetic fixtures in each enabled,
unset, disabled and native-math-disabled mode, four rounding modes, all x87 stack
positions, unusual float values, page boundaries and input aliases. Nineteen
rejection fixtures check that declined batches leave state untouched; seven
handoff fixtures verify the original scheduler behavior. Normal and ASan/UBSan
runs pass. Mutated loop/callee bytes reject the hook, and preprocessing with the
feature absent reproduces the original generated body exactly.

`tools/test_arm_model_palette.py` compiles with VitaSDK and executes the resulting
Thumb/VFP code in Unicorn's Cortex-A9 model. All 288 fixtures match the complete
context, arena and handoff counts. Ordinary fixtures exercise the existing native
matrix helper; a separate stack-boundary fixture exercises its translated
fallback. This distinction avoids inflating the apparent improvement.

For normal-layout, nearest-rounding fixtures:

| Matrices | Current path: counted instructions | Batch: counted instructions | Register copies, current → batch |
| ---: | ---: | ---: | ---: |
| 1 | 708 | 708 | 1 → 1 |
| 2 | 1,409 | 1,085 | 2 → 1 |
| 8 | 5,591 | 3,269 | 8 → 1 |
| 16 | 11,167 | 6,181 | 16 → 1 |
| 32 | 22,319 | 12,022 | 32 → 1 |
| 64 | 45,425 | 23,701 | 63 → 1 |

The 64-node current fixture includes one translated fallback. Counts exclude the
modeled bodies of memory-copy imports, which are reported separately. They are
neither CPU cycles nor frame times. Zero/unsupported counts incur an extra
experimental guard; ordinary builds have no added hook overhead.

The complete 48-scope native diagnostic builds successfully. Only `code_015.c`
changes among the 32 generated translation units; stripping the optional hook
restores the baseline generated output. The VPK passes archive integrity and
embedded executable checks.

In an isolated Vita3K run, the dashboard, profile model, normal solo Blood Gulch
startup, walking, camera turns and pause/leave flow work with the batch enabled.
The campaign reaches the cryo-room tutorial and responds to camera input; its
technician and room render. The nearby cryo-pod/body occlusion is also present in
the saved baseline capture. This is a startup check, not extended campaign,
vehicle, combat or hardware validation.

One stationary Blood Gulch window uses 480 accepted batches / 2,820 products in
60 frames, or 47 products per frame. Some windows fall back once for scheduler
budget exhaustion. A cryo-room window uses 780 batches / 3,720 products, or 62
products per frame. Bounds and layout rejections were not observed in these
samples. Hundreds of other matrix products still run through the current leaf
helper each frame, so this batch reaches only part of the workload. The emulator
is capped at 20 FPS; those timings do not establish a speedup on the Vita.

## Reproduction and next gate

Run the host script with `--xbe`, `--manifest` and a private `--output-dir`. It
requires the recompiler's Python dependencies. Run the ARM script with that
directory's `original.c` as `--reference`, a private `--output-dir`, and VitaSDK's
compiler as `--cc`; its Python environment also needs Unicorn and pyelftools.
Generated references and owned executable bytes must stay outside Git.

Before enabling this by default, inspect real model coverage and rendering, then
compare the same physical Vita scene with the batch off/on/off and diagnostic
timing disabled. Record whole-frame time as well as the affected work. The next
parallelism experiment must account for snapshot, dispatch and join costs and
preserve the first consumer's ownership; small batches may favor this serial path.

Private evidence is under `2026-09-12-222123-phase-followup/audit/model-palette`.
The experimental EBOOT SHA-256 is
`b6792c05c891f73f9cf36f22a55183c367c9aee5646e1d94581edbea7b146657`;
the package SHA-256 is
`14ef257390e007bcac9cc9f89190ffefb57942a9294d511f98b1b1b62a3ad547`.
No hardware installation was attempted while storage recovery was unresolved.
