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
