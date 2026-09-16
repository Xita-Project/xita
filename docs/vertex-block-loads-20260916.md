# Exact vertex comparisons with grouped NEON loads

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

## Hardware status

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
