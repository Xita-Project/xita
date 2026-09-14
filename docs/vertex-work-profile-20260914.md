# Vertex preparation: gameplay control and work sizing

The active objective is sustained 30 FPS on physical hardware through engine
restructuring. The earlier [campaign comparison](vertex-references-20260908.md#september-14-hardware-follow-up)
saved about 13.9 ms/frame by validating indexed vertex groups. The next step
tests that path in ordinary gameplay and sizes the remaining work before
introducing more worker jobs.

## Ordinary gameplay control

Dashboard **Graphics → Indexed vertex checks** exposes the existing
`XV_VERTEX_REFERENCES` setting, default **Off**. It is also available in the
in-game graphics panel. Edits persist for the next Xita launch; they do not
change a running game's upload policy. The setting preserves all referenced
vertex values, indices, shaders and draw ordering. No new worker is added by
this control.

Host checks verify selection, scrolling/wrap, selective setting preservation,
default-off behavior and the relaunch label. The in-game edit adds no drain or
live runtime change. In isolated Vita3K the option saves and the next launch
renders the campaign with indexed validation enabled and no reported upload
failures in the captured windows.

## Bounded diagnostic

Use a fresh build directory (or remove `build/runtime/xv_vertex_upload.o` when
switching this flag). Build with `XV_VERTEX_PROFILE=1` to collect elapsed caller time and requested
bytes for equal comparisons, unequal comparisons, initial snapshot creation and
upload dispatch. Each category is split into five span-size bins: up to 4 KiB,
4–16 KiB, 16–64 KiB, 64–256 KiB and above 256 KiB. The `[vertex-work]` records
are reported in the existing upload accounting window, then reset.

The recorder owns the counters. The existing worker still reads only retained
snapshots. No worker, GXM call, synchronization policy or allocation size changes.
The option is compiled out by default; ordinary builds make no new timer calls.
Missing clocks produce no samples. The tests use the production uploader with
independent expected bytes, exact boundary sizes, equal and changed sources,
retained older snapshots, counter reset and compiled-out/missing-clock variants.
ASan/UBSan checks pass, as do the existing indexed snapshot and delayed-worker
tests.

These are elapsed times including preemption and timer overhead. Requested
comparison spans are not actual memory traffic: unequal comparisons can stop
early. Snapshot time includes the existing caller copy/flush work for the
selected upload mode; dispatch measures submission or caller fallback, not the
asynchronous worker's completion. Do not add these numbers to overlapping pump
or GPU timings. Use a separate ordinary build for the final FPS comparison.

The diagnostic runtime SHA-256 is
`b899a5696f7472eeb0bf4252280458b9b2832252b78e7fc85a6b50dbfd3aa06d`.
Its package has the same 1,588 entries and updater contract as the confirmed
baseline; only `game-a.self` and `boot-game.txt` change. The final header and
rebuild produce the identical runtime hash tested in the emulator.

Private evidence is under
`2026-09-13-worker-sizing/validation/engine-restructure-20260914T2300Z`.
The gameplay handoff includes logs and the Captain Keyes cutscene screenshot.
Those mixed views are not a controlled optimization result.

## Physical diagnostic result

The exact diagnostic runtime booted on hardware and completed a fixed cryo-room
indexed-check off/on/off trial at 640 × 360. Taking the two complete 60-frame
accounting windows inside each arm (excluding the first straddling window):

| Caller work | Full checks before | Indexed checks | Full checks after |
| --- | ---: | ---: | ---: |
| Equal vertex comparisons | 27.089 ms/frame | 11.425 ms/frame | 27.130 ms/frame |
| Initial snapshot creation | 6.984 ms/frame | 7.028 ms/frame | 6.991 ms/frame |
| Upload dispatch | 0.207 ms/frame | 0.210 ms/frame | 0.211 ms/frame |

These instrumented timings size work; they are not an ordinary FPS result or a
new optimization gain. The ordinary runtime `88e8e7aef54a9882d543436ad501e4559dfd4bce79d95211dc9ff25acf8d63bf`
then booted through the updater on the physical Vita. It also renders the cryo
room in Vita3K with indexed checks enabled. Broader gameplay validation continues.

One Vita3K in-process restart crashed after the runtime logged completed GPU and
display drains and handed off to the boot helper. Restarting the isolated
emulator completed the staged update and verified the expected runtime. The
physical updater completed that same update normally.

The next experiment is [sharing large initial snapshot copies](snapshot-worker-20260914.md).
