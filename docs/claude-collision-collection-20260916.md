# Collision collection audit: 171F10

Status: one narrow native helper is integrated behind an optional build flag
and selector 43, default off. The review fixes pass host ASan/UBSan comparisons.
[ARM qualification](claude-object-collect-arm-qualification-20260916.md) found a
condition-flag difference; the integrated correction passes 544 strict ARM
comparisons. The remaining sections record the audit and its original handoff.
Two physical comparisons are complete (see §7). The steadier trial shows no
practical FPS improvement, so the helper remains off by default.

## Measured context

The physical campaign run `runtime3e49cf6f` sampled nested, inclusive times.
Those times include scheduling and owner-service parks:

| Trial | 172BF0 | 171F10 collection | 170C10 solver | Collection / 172BF0 |
| --- | ---: | ---: | ---: | ---: |
| 1 (25 samples) | 23,046 us | 18,644 us | 4,196 us | 81% |
| 2 (26 samples) | 18,059 us | 15,062 us | 2,791 us | 83% |

Per sampled call, collection takes about 580–750 us and solving 107–168 us.
These are times within sampled worker holds, not shares of a whole frame. The
sampler does not split collection among its children. Their relative costs below
come from the code, not from measurement.

## 1. Loops and call graph

`172BF0` reserves a 0xAC08-byte packet on its stack and calls
`171F10(flags, center, radius, a3, a4, ignore_datum, packet)`. `171F10`
adds the constant at `1F0F38` to the radius argument in place.

```
171F10  collect (frame 0x1014, stack probe 1D130)
├─ 88110  world BSP sphere query -> four lists on 171F10's stack
│   └─ 87EA0  recursive 3D BSP descent; plane-path stack in a 0x228 context
│       │     [87ECC plane distance: XV_NATIVE_BSP_SPHERE, already built]
│       ├─ leaf: append to leaf list (+C0C, cap 256)
│       └─ each 2D reference: linear search of the plane-path stack
│           └─ 87E10  recursive 2D BSP descent
│               └─ 86F50  per surface (all loops over the surface's edge ring):
│                   A  vertex distance² (SSE emulation, x_shufps calls)
│                      -> vertex list +808, linear dedupe ≤256
│                   B  B0CB0 segment/sphere test per edge (x87, guest call)
│                      -> edge list +404, linear dedupe ≤256
│                   C  projected point-in-polygon, only when A/B found nothing
│                   -> surface list +000, linear dedupe ≤256
├─ 868F0  (flags&20, 88110 hit) packet build from the three lists:
│   vertices 86440 -> [B5EA0] -> 855F0   sphere records (28 bytes)
│   edges    862A0 (convexity, 116F0) -> [B5E40/B5EA0] -> 851E0 capsules (40)
│   surfaces 86170 -> 86A40 ring + 11690 plane + [B5EA0] -> 85020 polygons (104)
└─ object walk (flags&80), 172034..172163:
    for each leaf in +C0C:
      cluster = leaf[+8]; skip if 2D2FB0[cluster] == 2D2FAC; stamp it
      for ref in chain 2FC6A0[cluster] -> 2FC6A4 records (+4 object, +8 next):
        object = 2FC6AC table; skip if object[+8] == 2FC684; stamp it
        push six args; call 1716F0(ECX=flags, EDX=datum)
          1716F0 re-resolves the same object, then:
            exit: ignored datum | flags bit0/bit24 | (B6&4 && type 0)
            exit: bounding sphere (50/54/58, radius 5C + arg) misses
            type bit (type+8) in flags, selector table 171944:
              type 0 biped          -> 487E0 + 855F0
              type 1 + 0x400000     -> 81900 -> 81770
              types 1, 6, 7, 8      -> 172DE0 -> 172F40 -> 88110 + 868F0
                                       (nested object-space BSP query/build)
              types 2-5, >8         -> nothing
            children +C8 (recursive), then siblings +C4 (back edge)
```

Collection writes the arrays `+000` (surfaces), `+404` (edges), `+808`
(vertices) and `+C0C` (leaves), each four bytes of count plus 256 entries.
`868F0` reads the first three. The object walk reads the fourth. Note that
`172F40` repeats the whole `88110`/`868F0` pipeline for each accepted
scenery, machine, control or vehicle object. The sampled 171F10 time therefore
includes nested BSP work for nearby objects as well as the world query.

Repeated work found in the original:

- **Walk → 1716F0:** each unvisited reference pushes seven guest words
  (six arguments plus the return address). It then builds a 0x74-byte frame,
  saves and restores four registers, and repeats the object-table lookup already
  done by 171F10. Before most rejections it also performs about seven lazy-flag
  updates and eight x87 pushes and eight pops, with paged float loads.
- **86F50:** each surface re-tests every vertex and edge of its ring.
  Neighboring surfaces share vertices and edges, so these are tested again and
  then discarded by the linear list searches. Those searches are O(n²) paged
  32-bit loads, up to 256 entries per list.
- **86F50 duplicate surfaces:** a surface reached from several 2D leaves repeats
  all three loops. This repeat is provably a no-op on the lists (see §3.2).
- **Object-space queries:** the `88110`/`868F0` pipeline repeats for each accepted
  model object, with added point transforms.

## 2. Existing work (avoid duplication)

| Existing | Covers | Relation |
| --- | --- | --- |
| `XV_NATIVE_BSP_SPHERE` (`xk_geometry.c`) | 87ECC..87EE5 plane distance only | In the measured build command; the rest of 87EA0/87E10/86F50 is still translated |
| `xv_bsp_plane_interval` | 88B80 segment traversal (1721B0 path) | Not on the sphere path |
| Typed cluster query (`xk_cluster_*`) | 56670 portal traversal | Shares the 2D2FAC/2D2FB0 epoch; its rebase helper is the model for private visitation |
| `xv_math_point_transform` (B5EA0) | Object-space transforms | Already native for 86440/86170/862A0 |
| Collision solver scope | 170C10 | Disabled; unaffected |
| `XV_NATIVE_OBJECT_SCAN` | 900E0 empty datum slots | Different loop; used as the pattern for this helper |

No existing work covers the 171F10 walk, 1716F0 or 86F50.

## 3. Proposed changes, ranked

### 3.1 Implemented: native object walk with exact 1716F0 early exits

`XV_NATIVE_OBJECT_COLLECT=1` adds a hook at `172034`. It is emitted only when both
complete spans match: `171F10` (672 bytes) and `1716F0` including its jump tables
(605 bytes). The hook calls `xv_object_collect_refs`, which runs the walk natively and
jumps to `L_00172163`.

**Preserved behavior:**

- Guest reads and writes in the original order: cluster stamps, object stamps
  and every reload of `39BE58`, `2D2FAC`, `2FC6A0`, `2FC6A4` and `2FC6AC`. The
  ignored-datum argument is read before the stamp write; the other arguments are
  read after it.
- Every object whose outcome is not established runs the **translated** `1716F0`
  with the same pushed frame, return address, registers and lazy flags.
- `X_PREEMPT` fires at both original back edges, with registers and flags
  synchronized. Budget use and the object-job stop behavior are unchanged.

The native path replaces a call only for callee exits that make no call and cross
no back edge. It still performs every guest store of that call, in the original
order, before any callee read:

- The seven pushed words (six arguments and the return word), shared with the
  translated route.
- The callee's four saved registers and its flag local at walk ESP−0x80.

Registers are then restored by reading those words back, as the original pops
do. Inputs that alias the frame, virtually or through a shared physical page,
therefore read the same bytes as in the original. The sibling at `+C4` must be −1
for every skip; the child-list paths also require `+C8` = −1.

- **A:** the object is the ignored datum.
- **B:** flag bit 0 or bit 24 is set.
- **C:** `B6&4` is set and the type is 0.
- **D:** the bounding sphere misses. This uses the same double operation order:
  `(dx²+dz²)+dy²` against `(r+arg)²`.
- **E/F/G:** the sphere hits, but the type bit is clear, the type is above 8, or
  the selector byte is 2.

At each observation point (translated call, preemption, loop exit), the helper
reproduces the full context:

- `EAX`, `ECX` and `EDX`, including `FNSTSW`'s low word on path D.
- All six x87 slots written below TOP, and the status word with TOP.
- Lazy-flag fields, including the CF/OF left by `shl eax,4`, `shl edx,cl` and `inc`.

Guest memory is identical, including the call frame; no region is assumed dead.
After a yield, both back edges resume their original targets unconditionally
(`1720C0` and `172040`) and reload every register. They never re-test a branch
that was already taken.

Non-finite floats and unmasked ARM FP traps leave the math paths to the translated
callee. Loop counts must be in `1..0x7FFF`, where the sign-extended 16-bit index
matches.

**Why it can reduce work:** each rejected object avoids the duplicate lookup, the
x87 and lazy-flag emulation, and the translated call. It still pays for 12 frame
stores and 4 reads. Every visited reference also loses its lazy-flag writes. The size of the saving depends on how many references each
walk visits and how many it rejects. That was **not measured**.
`xv_object_collect_report` logs walks, leaves, objects and skips per report
period, so an OFF/ON/OFF comparison can show whether skips occur at all.

### 3.2 Next: native 86F50 surface test, then its 87E10/87EA0 subtree

This is not implemented. The path handles the world query and every nested query
for nearby model objects. A native 86F50 would replace:

- SSE emulation of the vertex distance: `movss`/`movhps` loads,
  `subps`/`mulps` loops and two `x_shufps` calls per edge.
- A guest call to `B0CB0` per edge, with x87 emulation.
- The linear list searches.

Exactness requirements:

- Single-precision SSE arithmetic for the vertex distance; double x87 intermediates
  with float spills in `B0CB0` and the projected test.
- The projection axis table at `1EAF30` and the `+21C/+21E/+220/+224` context fields.
- List order and the 256-entry caps, the breakable-surface bitset, and the
  "hit" byte semantics.

An additional **exact skip** is available: when a surface is already in the
surface list, its first pass already tried to append every hit vertex and edge.
Lists never shrink during a query, and full lists stay full, so repeating the
surface changes no list. Evaluate private results first and publish them in the
original order. Leave `X_PREEMPT` at `87F18`/`87E7F`-class back edges in
translated code, or model them the same way as in 3.1.

### 3.3 Later: native packet writers (855F0 / 851E0 / 85020, 86A40)

These write fixed records with many paged stores and x87 spills. Rank them after
3.2 unless attribution shows `868F0` dominating.

**Attribution needed to rank 3.2 against 3.3:** add `88110`, `868F0` and
`172F40` to the existing sampled-interval list (`object_motion_profile.SPANS`).
Also enable the new helper's counters. This is a focused extension of the
existing sampler, not a broad benchmark.

### Rejected ideas

- **Cache results across frames or actors:** objects move and relink between
  calls (`8C9D0`/`8E970` after solving), and epochs advance on every call.
- **Unlock collection or run it in parallel as-is:** it writes the shared
  epochs, visited table, stamps and in-use bytes (§4).
- **Hash-set list dedupe:** lists hold at most 256 entries and must keep
  insertion order. A native linear scan over resolved pages is cheaper than
  clearing hash state per query.
- **Hoist global reloads out of the walk:** accepted objects run nested BSP
  queries between reloads, and proving that they preserve those roots saves only
  a few loads. The reloads stay.
- **Skip calls for objects with siblings or children:** the sibling loop has
  a back edge that preempts, and children recurse. These keep the translated call.
- **Prefilter accepted biped/model objects:** they call real work. The helper
  always calls the original for them.

## 4. Ownership and invalidation for threading

Collection is **not** a private computation. Keep it under the existing object
guard. It writes shared state:

| State | Written by 171F10 | Other users |
| --- | --- | --- |
| `2D2FAC` epoch (++), `2D2FA9` in-use byte, `2D2FB0[cluster]` | Cluster visitation | 56670 cluster query, 51A47/51D20/52240/52360 render, 8D320, 1721B0 |
| `2FC684` epoch (++), object `+8` stamps | Object visitation | 1721B0, 5A7B0, 8AA20, 8C7E0, 8CCB0, 8D020, 8FC90, 151B60, 153680, 153D80 |
| `[278248]+1` byte | Object-table iteration marker | 73 functions |
| 0xAC08 packet, lists, contexts | Caller and 88110 stacks | Private to the lane |

Reads that need invalidation if collection is ever captured:

- The structure/collision BSP roots `39BE58`/`39BE54`. BSP switch `58CD0` and
  map end `58440` retire them.
- The breakable-surface bitset.
- Cluster reference heads `2FC6A0` and records `2FC6A4`. Object movement relinks
  them (`8C9D0`, `8E970` in 4B9D0 after solving).
- The object table `2FC6AC` and object headers: creation, deletion, the flags
  `+4`/`+B6`, bounds `+50..+5C`, type `+64`, hierarchy `+C4/+C8/+CC`, and
  `+2A0/+424`.
- Each accepted object's model collision BSP and node transforms.

A future unlocked collection would need:

- Private cluster and object visited sets with epoch rebasing
  (`xv_cluster_query_rebase` is the model).
- Snapshots or generation checks of the chains and headers above.
- Publication of the stamp words under the guard.
- Object ordering compatible with 4B9D0, which writes actor state before
  `49600` and relinks afterward.

None of this is attempted here. The helper runs inside its caller's existing
transaction and takes no lock of its own.

**Native state of the helper itself.** 171F10 has several callers: `49600`
(the measured movement path, under the 4C980 object guard), `4D6C0`, `82380`,
`1050D0`, `172940` and `172BF0`. This audit does not prove that all of them
are serialized. The helper therefore keeps no plain shared native state:

- Configuration is one atomic word. The generated hook calls the helper without
  reading a global.
- Counters are tallied in locals for each walk and published with four atomic
  additions. Reports read them with atomic exchanges. A report is not a
  consistent snapshot of all four counts.
- A walk stopped inside a yield (an object-job budget stop) publishes no tallies.
- The FPSCR trap check reads per-thread state for each candidate skip.

Guest-memory races between unserialized callers would affect the original
translated walk in the same way. The helper adds none.

## 5. Implementation, validation and remaining risk

Changed files:

- `recomp/kernel/xk_object_collect.c`: the helper, its override and report.
- `games/halo_ce_3925/hooks.py`: signature-guarded hook at `172034`.
- `games/halo_ce_3925/runtime.mk` and `Makefile`: the `XV_NATIVE_OBJECT_COLLECT`
  build switch.
- `runtime/xv_ui_gxm.c`: periodic report under the same define.
- `tools/test_object_collect.py` and `tools/tests/object_collect.c`: the
  differential test.

The runtime switch is the environment variable `XV_NATIVE_OBJECT_COLLECT=1` or
`xv_object_collect_override()`. It is **not yet wired** to the benchmark selector
(`xv_benchmark.c`/`main.c`), which an OFF/ON/OFF hardware comparison needs.

The test lifts original `171F10`, `1716F0` and `1D130` from the owned XBE and
emits the hooked `171F10`. It then:

- Checks that each altered signature removes the hook and that the compiled-out
  hook preprocesses identically.
- Runs reference and candidate from identical randomized worlds. The worlds
  vary clusters, previously stamped clusters and objects, shared references,
  terminators, all exit classes, types including shift 0 and 31, selector bytes,
  children and siblings, NaN/Inf/extreme floats, grid equality cases, permuted
  noncontiguous pages and small preempt budgets.
- Aliases inputs to the walk's call frame: the query center virtually and
  through a shared physical page, and object headers virtually and physically.
- Uses yield callbacks that change scratch registers and carry/overflow. At chain
  back edges they set EAX to −1 or point EDX at another valid object; at leaf
  back edges they pick another valid leaf index.
- Compares the complete context, the callee's own return word and arguments, and
  every byte of the walk's frame window `[ESP−0xA0, ESP+0x20)` at every stub call
  and every yield.
- Compares the final context and every arena byte in all modes, with **no mask**.
- The first 16 cases are the review's frame-alias regression (center at walk
  ESP−0x80, no register-changing yields). The next 16 are the resume regression
  (EAX set to −1 at chain yields, no frame aliases). `--regression 1|2` runs
  only one kind.
- Requires skipped and called objects in ON mode, and zero admissions in the
  OFF and default modes.

To run it:

```sh
python tools/test_object_collect.py --xbe "$XBE" --manifest "$MANIFEST" \
    --output-dir "$PRIVATE/object-collect-host" [--sanitize] [--helper FILE] \
    [--regression 1|2] [--modes on off default environment]
```

The tool writes `result.json` (source hashes and per-mode output) into the
output directory.

**Not done:**

- A VitaSDK compile of the atomic builtins and helper, and a Cortex-A9
  instruction-count comparison, for example by extending
  `test_arm_bsp_sphere.py`'s Unicorn pattern.
- A TSan run of the counters.
- Benchmark selector wiring.
- A hardware comparison and gameplay/vehicle checks.

**Risks:**

- The stub harness models 1716F0's callees and cannot prove their liveness
  assumptions. Accepted objects always run the translated callee, so this
  affects only what the stubs expose.
- The synthetic worlds exclude object-table or chain pointers located inside
  the call frame. With those, the *original* follows frame words as handles and
  need not terminate.
- The per-walk saving depends on reference counts that no measurement has
  covered yet. If walks visit few references, the helper will not move frame time.
- The emitted alias function `f_00172078` duplicates part of the walk and stays
  unhooked. That is exact, but any calls into it get no speedup.

## 6. Independent review follow-up

The review (`object-collect-review/review.md`, helper `f3d8d58d…`) found three
issues. All three are fixed in helper `e17bc621…`.

1. **Omitted call-frame stores.** The original stores the flags at walk
   ESP−0x80 (`1716FB`) before reading the query center (`17175C`). A center at
   that address read stale bytes in the skip path and produced a different
   FSW/x87 state. The fix performs all 12 stores of the call in order before any
   callee read, then restores registers from them. It establishes no
   caller-specific aliasing precondition and needs none: skipped and translated
   calls store the same bytes. The old test mask over `[ESP−0x90, ESP)` is gone.
2. **Back-edge resume.** After `X_PREEMPT` at `172142`, the walk re-tested EAX.
   A yield that set EAX to −1 with EDX still valid therefore skipped an object
   that the original stamps. The chain loop now resumes `1720C0` unconditionally;
   its only exit is the pre-yield condition. No hardware occurrence is claimed.
   Current object-job yields stop the job, and owner yields were not observed to
   change EAX.
3. **Native data races.** A plain enable flag and plain counters were shared with
   callers that are not proven serialized. Configuration and counters are now
   atomic (§4). The shared guest transaction is unchanged.

Evidence, under `claude-collision-validation/` (each directory has `result.json`
with source hashes; test harness `3a57fe05…`):

| Run | Helper | Result |
| --- | --- | --- |
| `followup-host`, 3000 cases × on/off/default/environment | fixed | PASS; ON: 75,232 objects, 59,047 skipped, 238 alias-regression skips; yields (−1/datum/leaf) 1,728/1,610/13,031 |
| `followup-asan`, same with ASan+UBSan | fixed | PASS, no sanitizer output |
| `followup-positive-alias`, `--regression 1`, 200 × on/off | fixed | PASS; 3,415 of 3,575 objects skipped with the center at ESP−0x80 |
| `followup-positive-resume`, `--regression 2`, 200 × on/off | fixed | PASS; 1,178 chain yields set EAX to −1 |
| `followup-negative-reviewed-alias` | reviewed `f3d8d58d` | FAIL at case 0, first yield (frame window/event sequence differ) |
| `followup-negative-reviewed-resume` | reviewed `f3d8d58d` | FAIL at case 0 |
| `followup-negative-mutant-flag-local` | fixed, minus only the `1716FB` store | FAIL at case 0 |
| `followup-negative-mutant-resume` | fixed, but re-tests EAX after the yield | FAIL at case 0 |

Host and ASan runs differ slightly in event and object totals. The synthetic
world's yield decisions hash the whole context struct, including its padding.
Reference and candidate always see identical bytes within one build.

The reviewer's own witness (`test-alias.py`) was not re-run. It calls the old
`reject_object` signature and asserts that the mismatches are still present, so
it is not a regression runner for the fixed helper.

## 7. Physical comparison after integration

Runtime `006ef92fbd37dd93a67ce897bdfff6414901f2c2a655006fcbb831d05e04db6b`
is installed and boot-confirmed through the updater. Only the runtime and its
boot marker changed; the launcher, assets and graphics configuration are retained.
Selector 43 completed two same-session comparisons in a loaded campaign room at
544p, with existing workers enabled. Both restored the original disabled mode;
neither comparison logged STOP/FATAL or a boundary failure.

| Trial | OFF before | Native ON | OFF after |
| --- | ---: | ---: | ---: |
| 1 | 7.347 FPS | 7.999 FPS | 7.667 FPS |
| 2 | 8.331 FPS | 8.389 FPS | 8.366 FPS |

Trial 1 has a drifting baseline. The steadier second trial improves only 0.49%
relative to the mean of its two OFF arms, which does not establish a practical
performance benefit. No gain is promoted and this helper stays disabled.

The periodic counters confirm real admissions: 34,883/39,551 objects skipped
(88.2%) in trial 1 and 36,471/38,641 (94.4%) in trial 2. Those totals include
report windows crossing settling and measurement; they are not exact timed-arm
counts. The camera was held within each trial, while live NPC simulation continued.
The restored checkpoint position/view differs from the earlier approximately
11.5 FPS diagnostic capture, so the absolute results are not a controlled
cross-build regression comparison.

Next compare the separate native vertex pass inside `86F50` with this object
walk disabled. Claude's next independent task examines `B0CB0` segment/sphere
math in the later edge pass, without changing vertex work or actor ordering.
Evidence: `object-collect-candidate/physical-campaign-native/{result,analysis}.json`.
Stable 20 FPS and the reported gameplay crash remain unqualified.
