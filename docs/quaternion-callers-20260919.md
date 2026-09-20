# Remaining quaternion callers on hardware

Perf.43 / `d8c2d31` retains the cumulative perf.42 selections and adds only
`XV_OBJECT_QUAT_PROFILE=1`. The updater verified 32,208,722 bytes, restarted
Xita and confirmed slot 1. Runtime SHA-256:
`fa85a080b5b3e4e00ffaf182058838f980325c2ca178dcd270dd7999dd9b68b0`.
Remote status and the dashboard confirm the installed version.

This is caller attribution during ordinary campaign gameplay. No automated
FPS comparison or optimization toggling is involved. Counters identify work;
they do not establish an FPS improvement or time saved by removing a lock.

The quaternion helper already captures four input floats and constants while
holding the guard, then releases it before arithmetic when output and scratch
are worker-private. The private-input bypass remains enabled. Remaining shared
inputs require a lifetime/ownership solution, not simply deleting acquisition.

## Offline caller inventory

The owned generated guest code contains these return PCs. These are guest
addresses and must not receive the native executable relocation correction.

| Guest return PC | Caller / input pattern |
| --- | --- |
| `3EBA9` | `3EA70`, stack temporaries |
| `825C3` | `824B0`, shared object record |
| `84DEA` | `84D40`, shared object record |
| `8E130` | `8DDF0`, hierarchy loop with private output |
| `8E55F` | `8DDF0`, alternate hierarchy output array |
| `A1F6D` | `A1EC0`, record conversion |
| `A2085` | `A1FE0`, indexed pose array / private output |
| `A2163` | `A213A`, record / private output |
| `A36BB` | `A3600`, indexed pose array / private output |
| `A5036` | `A5010`, private input / external output |

The existing native hierarchy hook at `8E0F0` already batches part of the
`8DDF0` loop. It does not cover `A1FE0`, `A213A` or `A3600`.
`A1FE0` walks child/sibling links, composes each node with its previously
published parent matrix, and uses a private worklist. A future batch must
preserve this dependency order, all guest continuation state and exceptional
floating-point behavior. It cannot treat nodes as independent tasks.

## Counter scope

`object-quat-site` records calls only after the existing private-output/scratch
admission and at guard depth one. Already bypassed private-input calls and
shared-output calls are not a complete part of this census. Tables reset after
each report; overlapping downloaded tails must not be summed as fresh windows.
Overflow counts must be checked. Frequency alone is not lock hold time.

Private deployment, launch and gameplay receipts are in
`../quat-callers-hardware/`. Gameplay attribution follows below.


## Ordinary campaign capture

The campaign loaded and the captured image confirms world geometry, pistol,
reticle and HUD on perf.43. Controls were released. Recent ordinary frame
windows were 12.6–12.8 FPS; this is not a comparison result or a new FPS gain.

The final complete caller report contains:

| Guest return PC | Lane 0 calls | Lane 1 calls | Combined / 60 frames |
| --- | ---: | ---: | ---: |
| `A1F6D` | 2,899 | 2,501 | 90.0 per frame |
| `8E130` | 1,273 | 1,127 | 40.0 per frame |

All four entries report zero private inputs and zero overflow. `A1EC0` accounts
for 69.2% of this admitted shared-input census. None of `A2085`, `A2163` or
`A36BB` appears in this report. This changes the next target to `A1EC0` rather
than the initially inspected pose hierarchy variants.

The inspected `A1EC0` loop reads 32-byte source records, applies an optional
byte-selection filter and optional node remapping, and writes 108-byte output
records. Each admitted record converts a quaternion, copies translation,
multiplies by a selected 52-byte matrix, and optionally negates three output
components. It also has a signed output-capacity limit and guest preemption
at the loop backedge. The sampled output addresses are worker-private; source
addresses are shared. Source immutability is not established by their addresses.

Next implementation target: capture the admitted source records and required
matrices under the existing ownership boundary, then prepare private output
records in a batch. Establish source lifetime, filtering, aliasing, capacity,
preemption and full guest continuation semantics first. Preserve fallback for
unsupported inputs. Do not extend the lock across all arithmetic or remove it
merely because source records appear constant during one run.

Receipts: `gameplay.log`, `gameplay.png`, `gameplay-status.json`,
`installed-status.json`, `deploy.log`, plus the private extracted caller body.
This short observation does not establish long-session stability or 20 FPS in
heavy gameplay. The diagnostic flag should be omitted from the next actual
optimization build after its caller evidence has been retained.


## Snapshot arithmetic extraction

`xk_quaternion_snapshot.h` now holds the existing arithmetic as an inline
function over explicit captured inputs, constants, output and scratch. It still
updates the caller-owned x87/register/flag state exactly as the leaf requires.
It does not translate guest addresses, acquire a lock, allocate, or update
shared counters. The existing `xk_math.c` wrapper retains admission, input
capture, synchronization, cache policy, accounting and guest return handling.
The Makefile explicitly tracks the new header dependency.

This is the arithmetic prerequisite for the marker batch, not the completed
batch and not a performance gain. There is no new deployment for this extraction;
perf.43 remains the installed runtime. The capture/publication bridge still
needs to establish the source-record/matrix ownership and continuation contract.

Validation:

- 4,096 original guest / native wrapper / snapshot cases under ASan/UBSan cover
  four rounding modes, random and exceptional floats, changed constants, guest
  context, 52-byte output and 24-byte spills. Snapshot/wrapper equality is exact,
  including native FP status. Original/native arithmetic NaNs use the existing
  payload-normalization contract. Guest mappings are null during the direct
  snapshot call, proving that path does not read the guest arena or page table.
- Production worker tests pass all 40 configurations with 600 callbacks each,
  ASan/UBSan, compile-default quaternion selection, held-guard and owner-service
  cases. This validates preserved existing ownership behavior; it does not prove
  a future marker snapshot's ownership.
- The VitaSDK-linked quaternion/cache suite passes 1,920 ARM cases across
  rounding and FP controls against the independent original guest lift. These
  are arithmetic correctness tests under Unicorn, not Vita3K or FPS comparisons.
- A stale ARM harness initially failed to link because the two math units both
  exported point-transform symbols. Renaming the baseline's two point symbols
  fixed the harness. No runtime symbol behavior changed for that repair.

Private receipts: `../quaternion-snapshot-tests.log`,
`../quaternion-snapshot-workers.log`, `../quaternion-snapshot-arm.log` and their
artifact directories. Generated original code stays private. Reproduction:

```sh
python tools/test_quaternion_snapshot.py --xbe OWNED_XBE \
  --manifest OWNED_MANIFEST --out PRIVATE_DIRECTORY
```


## Combined marker-record prototype

`xk_marker_snapshot.h` now computes the `A1F5F..A1F9A` quaternion-plus-matrix
segment from one owned packet. It produces the two 52-byte transforms, node
index, final 32 bytes of guest call-stack spills and exact continuation context.
The caller retains destination bytes 2..3. Guest addresses retained in the
packet/context are used only to reproduce register and spill values, never to
fetch inputs. Filtering, remapping selection and the optional later sign flip
remain outside this segment.

The shared matrix arithmetic was extracted into `xk_matrix_snapshot.h`. The
existing matrix wrapper retains its guards, layout checks, capture, NEON
selection, counters, publication and return handling. Marker computation uses
the scalar snapshot path. Enabling that prototype alongside NEON requires its
own admission/behavior qualification; the ARM marker test uses the scalar path.

Validation after this extraction:

- 1,024 marker fixtures under ASan/UBSan: signed node indices, nonzero upper
  register bits, exceptional floats, all rounding modes and x87 TOPs. Candidate
  equals the existing native path in complete context, all 4 MiB of guest
  memory and native FP status. The independently lifted original additionally
  matches context/output using the existing arithmetic-NaN normalization.
- Existing leaf fixture: 120,000 original/native comparisons and another
  120,000 disabled/fallback comparisons, including exact in-place and physical
  aliases and rejected layouts. Its host microbenchmark is not hardware evidence.
- 256 VitaSDK-linked ARM current/snapshot cases: complete context, 2 MiB arena
  and FPSCR controls/sticky exceptions, including FZ/DN and rounding variations.
  Firmware copies are modeled; this is arithmetic validation, not Vita3K testing.

The first host check caught an unnecessary ADD flag publication. The admitted
lift leaves that ADD's flags dead and retains the preceding IMUL flag state;
the prototype now preserves that representation. Any future hook must validate
its emitted continuation rather than assuming this for another lift variant.
The first ARM harness also inlined its dummy memory-copy bodies; putting those
stubs in a separate translation unit restored the modeled copies and the checks
passed. Neither failure was a hardware rendering result.

Reproduce with `tools/test_marker_snapshot.py --xbe OWNED_XBE --manifest
OWNED_MANIFEST --out PRIVATE_DIRECTORY`, then
`tools/test_arm_marker_snapshot.py --reference PRIVATE_DIRECTORY/reference.c
--output-dir PRIVATE_ARM_DIRECTORY --cc arm-vita-eabi-gcc`.
Private receipts: `../marker-snapshot-tests.log`, `../marker-snapshot-arm.log`.

Still required before deployment: a guarded capture/admission bridge proving
private destination and stack mappings, disjoint input/output/scratch, safe
source capture and publication; exact-image/emission hook gating; production
worker tests exercising that bridge. The candidate is not enabled, no lock has
been removed from gameplay, and perf.43 remains installed. This prototype
combines one marker's two calculations; it is not yet a whole-list batch.

The existing production-worker suite also passes 40 configurations × 600
callbacks under ASan/UBSan after the matrix extraction. Receipt:
`../marker-snapshot-workers.log`. The future capture bridge is not covered yet.


## Capture bridge and exact guest hook

`xk_marker.c` now captures the source record, selected matrix and constants under
one existing math transaction, then releases it for private calculation and
publication. `xv_object_marker_admit` requires the real worker thread, its exact
live context, depth one, unchanged private stack pages for output and scratch,
and the existing private-math policy. Hold profiling, nested transactions and
foreign contexts decline. Bounds, page crossings and physical overlaps retain
the original path without changing guest state. No shared pointers survive into
the arithmetic kernel. The original filter, remapping selection, loop order,
optional sign flip and preemption remain in guest code.

The hook gates the complete `A1EC0` body and both math leaves by owned-image
signatures, then requires an exact emitted-region hash. The absent build flag
preprocesses to the original body. Explicit `XV_NATIVE_MARKER_RECORD=1` requires
a Halo CE recompilation with object workers; config stamps rebuild affected
objects on changes. The runtime environment can explicitly disable it, and
`XV_NATIVE_MATH=0` retains the original path. Ordinary builds default off.
`[marker-snapshot]` reports captured records per worker after retirement.

The production-worker fixture checks 64 direct captures and another 64 through
the actual emitted hook, across both worker lanes. Full context and private
memory match the guarded reference. Replacing each lane's source after the
capture lock is released leaves its results unchanged. The fixture also checks
owner-thread service admission and declines for copied contexts, nested guards,
shared/foreign output, changed pages, page crossings, overflow and aliases.
ASan/UBSan pass. Original-image mutations, emitted-region drift and disabled
preprocessing gates pass in the same runner. Receipt:
`../marker-worker-tests.log`; reproduction adds `--workers` to the marker test.

The ARM suite with `--neon` additionally qualifies the existing NEON-enabled
matrix wrapper against the scalar captured record. It requires nonzero NEON
admissions rather than merely compiling that option. This is functional/FP
qualification, not a promise that the scalar packet is faster than NEON math.
Receipt: `../marker-arm-neon.log`.

This replaces two math acquisitions with one for admitted records. It does not
batch a complete marker list or change simulation/frame buffering. The next
cumulative package will retain earlier optimizations, remove the now-completed
quaternion caller census and enable this candidate. Hardware activation,
rendering, long-session stability and FPS effects remain unverified.


## Perf.44 deployment receipt

The cumulative perf.44 / `6e89966` package retains perf.43 optimizations, removes
`XV_OBJECT_QUAT_PROFILE=1` and adds `XV_NATIVE_MARKER_RECORD=1`. The new guest
hook is present in the retained shard and the linked ELF contains the capture
and admission functions. Only `game-a.self` and `boot-game.txt` differ from the
previous package; package member names remain identical.

Runtime SHA-256:
`1dd4a22863243c3bdf0667bafe77d0020fa11dd7350ae5c34b6f8e2590d88b1d`.
VPK SHA-256:
`7547a6742a3b50a5b7b49d78164146768b50212ecd21dc471caef8ebe030c534`.
The updater verified 32,213,946 bytes, restarted Xita and boot-confirmed slot 0.
Remote status and the dashboard image confirm perf.44 / `6e89966`.

The normal campaign sequence has started. Capture activation, gameplay rendering,
frame times and long-session stability remain pending; deployment is not a claim
of a performance improvement. Private receipts are in
`../marker-capture-hardware/`. Its `inherited-perf43/` folder contains copied
prior-run receipts, not observations from perf.44.
