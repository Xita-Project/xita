# Indexed vertex validation experiment — September 8, 2026

The native visibility experiment did not improve the collected hardware result.
The next candidate targets repeated vertex comparisons during draw preparation.
It is **off by default**. The September 14 hardware follow-up measured a
repeatable gain in the Pillar of Autumn cryo room; see the results below.

## What changes

Halo often draws a small subset of a larger vertex buffer. The existing uploader
keeps a CPU snapshot and an owned GPU copy, then compares the requested vertex
span before reusing that copy for another draw in the same frame slot.

The candidate builds an exact coverage mask from the same cached index chunks
copied to the GPU. Each mask bit covers eight consecutive vertex records. For
sparse spans of at least 512 vertex records, comparisons cover those groups
instead of the whole span. Adjacent groups share one comparison. Dense requests,
unrecognized attribute formats, mismatched strides and attributes extending past
their record use the existing full-span comparison.

Every vertex referenced by the retained index list is still checked. The uploader
reuses a snapshot only if all checked bytes match; if none matches, it appends a
new complete owned snapshot. An
unreferenced change may reuse an earlier snapshot, but a later draw referencing
that record must check it again. Already published snapshots stay immutable
until their frame slot retires. No guest pointers are submitted directly to GXM,
and index values, vertex values, shader transforms and draw order are unchanged.

There is a cost: building the mask adds work to index capture, and fragmented
coverage needs more comparison calls. Fewer bytes compared is not automatically
faster. The controlled test includes both costs.

## Capture evidence

Four explicitly captured frames supplied 876 non-immediate stream-0 draws. An
offline cache replay using eight-record groups produced these comparison spans:

| Captured frame | Full-span bytes | Grouped bytes | Reduction |
| --- | ---: | ---: | ---: |
| 3632 | 3,544,256 | 2,565,568 | 27.6% |
| 5872 | 5,063,488 | 3,325,760 | 34.3% |
| 8144 | 4,461,952 | 1,754,752 | 60.7% |
| 12320 | 581,664 | 544,288 | 6.4% |

These are requested comparison spans from captured data, not cache misses,
physical memory traffic, measured cycles or predicted FPS. The replay covers
only captured stream 0 and assumes a compatible layout; live layout checks can
fall back. It excludes immediate draws and does not model timing, early exits
inside equality helpers, GPU work or index-mask construction cost.

Reproduce it with `tools/analyze_vertex_references.py LOG MESH_DIRECTORY` using
locally captured game data. Do not commit mesh dumps or game data to the repo.

## Correctness checks

- Host ASan/UBSan tests exercise index retention, all 65,536 index values,
  unaligned addresses, referenced and unreferenced mutations, aliases, and 4,000
  draws across 500 frame-slot generations. Every fetched record and all earlier
  GPU snapshots are checked against independent expected bytes.
- Production retention/layout checks exercise fallback paths; all 16 benchmark
  selector combinations check normal completion, cancellation and view loss.
  Frame acquisition/completion checks cover the new override branch.
- Vita-compiled ARM helpers pass 232 capture cases and 429 total calls with
  strict read/write bounds. Firmware copy/fill calls are modeled. The test's
  Thumb conditional-store self-check detects an instrumentation regression in
  Unicorn 2.1.4; the full bounds checks pass with Unicorn 2.1.3. Do not disable
  memory hooks to obtain a passing result.
- The native VPK contains the same 1,584 non-executable payloads as the verified
  hardware baseline. The private emulator installation was checked against all
  1,585 candidate payloads before testing.

From the repository root:

```sh
make -C recomp/host test-frames test-vertex-references
python3 -m venv /tmp/xita-arm-tests
/tmp/xita-arm-tests/bin/pip install unicorn==2.1.3 pyelftools
export PATH="$HOME/vitasdk/bin:$PATH"
/tmp/xita-arm-tests/bin/python tools/test_arm_vertex_references.py --output-dir /tmp/xita-arm-results
```

The private Blood Gulch off/on/off run completed at 19.969 / 19.966 / 19.970 FPS
with the 20 FPS cap unchanged, a comparable camera and 120 measured frames per
phase. A second run was cancelled and restored the configured setting. The
counters show selective checks during the enabled phase and none while disabled.
This establishes benchmark operation in the emulator, not hardware performance.
At that spawn view, enabled index preparation rose from about 0.182 to 0.323 ms
per frame while stream preparation fell from about 0.380 to 0.304 ms. Their
combined elapsed time therefore increased slightly in this emulator sample.
That tradeoff is a reason to measure hardware and heavier campaign views before
enabling the candidate by default.

The campaign cryo-room comparison also completed with a comparable camera and
120 measured frames per phase: 19.962 / 19.962 / 19.934 FPS under the same cap.
Full-span comparison requests were about 5.82 MiB per frame; selective validation
reduced them to 2.69 MiB. Index plus stream preparation fell from about 5.94 to
4.34 ms per frame in this emulator sample, including mask construction cost.
These September 8 measurements cover the emulator. Physical results follow.

Private checks covered Blood Gulch walking, camera turns, rifle/grenade effects,
the flashlight, Warthog entry/driving/exit and campaign camera movement. Six
explicitly sampled frames checked 1,240 draws in total with no snapshot changes
before GPU completion. These samples do not establish hardware stability or
resolve the earlier hardware rocket/death crash. Death/respawn was not confirmed
in this run.

## Hardware comparison

Keep the current graphics settings and use the candidate executable. Set:

```ini
XV_VERTEX_REFERENCES=0
XV_BENCHMARK_VERTEX_REFERENCES=1
XV_BENCHMARK_NATIVE_BOUNDS=0
```

In Blood Gulch or campaign, face a representative scene and stop moving. Press
**L + R + Square**. The test runs full/indexed/full validation at the current
resolution, with 60 settling frames followed by 120 measured frames per phase.
It checks camera comparability and restores the configured setting afterward.
Resolution, effects, frame cap and the other optimization overrides stay fixed.

The `[vertex-references]` counter reports checks, hits, comparison runs and
requested/compared KiB; `[draw-prep]` reports index and stream preparation times.
Judge the complete frame time as well as those individual stages. A separate
run with `XV_VERTEX_REFERENCES=1` is needed for driving, firing, death/respawn and
campaign correctness. Keep it disabled if hardware shows no useful improvement
or any rendering/lifetime regression. Sustained 20 FPS remains the project goal.

## September 14 hardware follow-up

Three physical cryo-room comparisons at 640 × 360 completed with passing camera
checks. Each arm used 60 settling frames and 120 measured frames. The frame cap
was 30; texture maximum was 128, low model detail and triple buffering were
enabled, and the existing vertex copy worker stayed enabled throughout. The
requested 500 MHz CPU clock resolved to 444 MHz; GPU/bus/crossbar were
222/222/166 MHz. Query-overlap, native bounds and model-palette experiments were
disabled. Live animation and simulation continued.

| Trial | Full before | Indexed | Full after |
| --- | ---: | ---: | ---: |
| 1 | 6.220 FPS | 6.782 FPS | 6.185 FPS |
| 2 | 6.093 FPS | 6.674 FPS | 6.134 FPS |
| 3 | 6.206 FPS | 6.772 FPS | 6.151 FPS |

Pooling elapsed microseconds across the 720 full-validation and 360 indexed
frames gives **6.165 → 6.742 FPS**, or **162.216 → 148.317 ms/frame**. The
candidate saved **13.899 ms/frame**, a 9.37% FPS increase. Individual trials
saved 13.777, 13.737 and 14.183 ms/frame.

Nearby draw-preparation windows agree with the direction of the full-frame
measurement: stream preparation fell from about 34.92 to 19.12 ms/frame, while
index preparation rose from 3.69 to 6.08 ms/frame. Those counter windows do not
align exactly with the measured arms, so their difference is supporting
evidence rather than an independent exact accounting of the gain.

The test runtime SHA-256 was
`5e5e120eeea33e0f11a51ac9f63b05f69df526c99424a7883c5c9f8039f06619`.
Private logs and exact-time analysis are under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/flare-query-overlap/`:
`physical-campaign-references`, `physical-campaign-references-analysis.json` and
`physical-campaign-reference-costs.json`.

This confirms a benefit in one demanding campaign view. It does not establish
the campaign average, stable 20/30 FPS, or correctness throughout driving and
death/respawn. Comparisons restored `XV_VERTEX_REFERENCES=0`; the physical device
does not silently keep the experiment enabled afterward. The next validation
is ordinary gameplay with indexed validation enabled, followed by the same
comparison in additional campaign and multiplayer views.
