# Exact vertex comparisons with grouped NEON loads

The earlier hardware trials below were inconclusive. A September 17 cumulative
fresh-launch build is now installed with a process-start build default; see the
follow-up at the end. The comparator itself is unchanged.

The experiment changes how the CPU loads bytes while validating a retained
vertex snapshot. It compares the same 64-byte groups, stops on the same group
with a mismatch, and uses the original 16-byte/scalar tail. It preserves sparse
reference coverage, byte equality, immutable snapshots, upload workers, GPU
retirement and draw order. It does not omit vertices or change graphics quality.

The original compiler output uses eight 16-byte loads and several scalar
address calculations per 64-byte pair. `vld1q_u8_x4` expresses each side as a
group, letting the current Vita SDK emit four 32-byte loads. Neither version
assumes alignment or reads past the requested spans. All operations are integer
loads and bitwise comparisons; matrices and shaders are untouched.

`XV_VERTEX_BLOCK_LOADS` defaults to **0**. Benchmark **41**, `vertex-blocks`,
runs original/grouped/original comparisons at the current resolution. It checks
that the NEON comparator is available, switches at the drained recording
boundary, and restores the initial effective setting on completion, cancellation
or loss of the gameplay view. The network handler only publishes the request;
it does not initialize or change comparator state. Other comparison selectors
retain their numeric values, including diagnostic-hist 294.

`[vertex-block-loads]` records enabled state, comparison calls and requested KiB
in the existing 60-frame report. Requested bytes are not bus traffic: a mismatch
can exit early. There are no new per-comparison clock calls. The experiment is
restricted to vertex validation; constant comparison and index copying retain
the original helper.

## Validation

The Vita-compiled comparator, original comparator and installed Vita libc pass
21,997 cases and 66,043 calls in the ARM instruction harness. Checks include
all alignment pairs around vector/tail boundaries, mismatches in every byte of
short spans, larger equal/changed spans, same-pointer and empty spans, and
inputs ending at unmapped pages. Read/write bounds hooks remain active; there
are no out-of-range accesses. This is not Vita3K or a hardware speed result.

| Equal span | Original comparator instructions | Grouped loads |
| --- | ---: | ---: |
| 64 bytes | 46 | 40 |
| 256 bytes | 139 | 109 |
| 4,096 bytes | 1,999 | 1,489 |
| 65,536 bytes | 31,759 | 23,569 |

Counts include the common test wrapper's selector branch. Fewer instructions
do not establish fewer hardware cycles or an FPS gain.

ASan/UBSan checks run the actual uploader through 4,000 draws across 500 slot
generations with the option off and on. They compare all referenced records
and previously published GPU snapshots after source mutations and slot reuse.
The host uses the portable byte-comparison fallback; the ARM tests cover the
NEON instructions. Actual delayed upload/preparation worker tests pass with
the option enabled, including 600 generations and notification/dispatch failures.
Controller tests cover off/on initial states, completion, cancellation, lost
view and unavailable/absent implementations. Production frame-boundary and
HTTP/client tests cover the new selector without changing other modes.

```sh
python3 tools/test_vertex_blocks.py
python3 tools/test_frame_acquisition.py
python3 tools/test_remote.py
XV_VERTEX_BLOCK_LOADS=1 make -C recomp/host test-upload-worker test-vertex-prepare
python tools/test_arm_bytes_equal.py --blocks \
  --baseline-elf /private/installed-build/build/xita.elf \
  --output-dir /private/vertex-blocks-arm
```

The ARM tool needs the existing Python ARM-test dependencies and Vita SDK GCC
on PATH. The baseline ELF supplies Vita libc for the independent result check.

## Earlier hardware trials

Runtime `c55b191b04025461fe1504c0f2e1780ff863c9c0b14fd034291efec9c0e97a6e`
has been uploaded, verified, boot-confirmed in slot 1 and launched through Halo's
main menu. Its 1,588-member package changes only `game-a.self` and `boot-game.txt`;
the asset/updater contract and native-math object match the preceding build.
Only the uploader, remote handler, benchmark controller and main runtime objects
change. Timing instrumentation is not compiled in. The feature remains off
outside its comparison.

The first launch reads 360p, 128-pixel textures, reduced material/model/effect
settings, and several disabled visual switches from the user's saved config.
These settings are separate from the standard native-resolution baseline.
Two physical comparisons now complete in a stationary Pillar of Autumn view:

| Trial | Original before | Grouped loads | Original after |
| --- | ---: | ---: | ---: |
| 1 | 12.045 FPS | 13.799 FPS | 13.665 FPS |
| 2 | 13.872 FPS | 13.217 FPS | 13.517 FPS |

Both pass the controller's camera-consistency check and restore the original
comparator. Measured-window reports confirm grouped comparisons execute only
in the middle arm. However, simulation continues and draw counts vary; the
first trial's original arms also drift substantially. Paired frame-time changes
are a 5.63 ms saving and a 2.63 ms regression. This is **not a repeatable gain**,
so the candidate stays disabled. Stream preparation remains around 3.3–4.0 ms
per frame in these report windows, with no clear reduction from grouped loads.

There is no crash during these two stationary trials. That does not establish
driving/combat stability or the stable 20 FPS goal. The next optimization should
target the larger measured scene/object preparation costs; the ARM instruction
reduction alone does not justify enabling this comparator.

Private receipts and the preserved pre-update gameplay log are under
`direct-cluster-query/vertex-block-loads/` in the September 14–16 validation
directory. `package.json`, `upload.json` and `confirmed.json` distinguish a
verified transfer from a confirmed boot. `physical-campaign-360/result.json`
and `analysis.json` record the completed comparisons and their limitations.

## September 17 cumulative startup retest

`XV_VERTEX_BLOCK_LOADS_DEFAULT=1` now selects the existing grouped comparator at
process start. The repository build default remains zero. An explicit
`XV_VERTEX_BLOCK_LOADS=0` environment/config setting takes precedence, and the
existing comparison controller restores the original effective mode after its
run. The hardware retest uses a fresh process and ordinary gameplay instead of
invoking that controller. Existing periodic reports establish admission through
`enabled 1` and nonzero block checks.

The build stamp changes only the uploader object when switching the startup
default; repeating the same mode is a no-op. Invalid defaults are rejected.
Replacing the Makefile in the private staging copy initially regenerated a
byte-identical layout header and rebuilt main; the subsequent incremental checks
start from that settled stage. No generated guest instruction object changed.
The grouped archives were recreated with the same ordered member bytes.

Actual uploader tests pass with ASan/UBSan for both compiled defaults, explicit
environment overrides and restoration, including mutations and retained older
GPU snapshots. The unchanged comparator matches the prior ARM validation source,
so no new ARM instruction result is claimed. Independent review found no change
to equality semantics, reference coverage, source snapshots or GPU retirement.
The full Vita build passes; its package changes only the runtime and boot record.

This candidate adds grouped comparisons to the five retained startup paths and
the preceding worker/rendering changes. Standard graphics remain the baseline.
It does not claim that the earlier benchmark arithmetic was broken or that the
combination improves FPS. Private build/review receipts are under
`direct-cluster-query/vertex-block-startup/`.

The runtime from source `ca0f3b0` is transfer-verified and boot-confirmed in
updater slot 0:
`a4e35d47a6d5db8d11e4915708ce93b9e900a8bc1893baf62a2cb61f98859eda`.
The preceding five-path `31af4cf…` remains in slot 1. Startup logs confirm the
prior paths and grouped comparisons enabled together, with native 960 × 544
rendering, Original graphics and effective CPU/GPU clocks of 444/222 MHz.
Ordinary cryo-tutorial gameplay reports nonzero grouped checks (84,240 in one
60-frame window); the new path is executing without a benchmark mode switch.

The first launch loaded save directory `122A17771B9F`, whereas the preceding
pistol checkpoint loaded `122A17771B9E`. Subsequent screenshots identify the
first selection as New002 and the pistol profile as New001; the original
assumed New001 label in the private receipt was corrected. The cryo view reports
roughly 496–506 draws/frame and 5.8–5.9 FPS; the earlier pistol checkpoint had
about 150 draws/frame. Those different workloads cannot establish a gain or
regression from grouped comparisons. The candidate remains enabled for
cumulative gameplay evaluation. A brief right-stick camera and pause-menu
check also completes, with input released and no logger error. This does not
qualify combat or driving. No five-FPS gain or long-session crash fix is
established by these startup and admission checks.

Selecting New001 explicitly through the normal campaign menus restores the
pistol checkpoint at `(-28.66, 32.52, 0.62)`, facing `(0.56, 0.82, -0.15)`.
An ordinary displayed-frame observation then records 767 frames over 60.051 s,
or **12.7725 FPS**, versus the preceding five-path observation's 12.7689 FPS.
The grouped comparisons execute and the other paths remain active. This is
inconclusive, with no meaningful improvement or regression established in that
view. Live AI, draw counts and loading histories remain uncontrolled; request
latency bounds do not quantify that workload variation. All six paths stay
enabled. Receipts are `pistol-passive/result.json`, `pistol-analysis.json`,
`new001-gameplay.log` and the explicit profile/checkpoint screenshots.
