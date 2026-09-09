# Development tracing overhead — September 9

The current development build records the active guest function on every entry
and after every direct call. The recompiler already supports omitting those
markers. This audit builds that existing mode, on top of the
[parity optimization](parity-flags-20260909.md), without changing graphics settings
or removing game instructions.

The candidate is staged on the private `work/2026-09-09-trace-overhead` branch.
It is **not installed on a Vita**, and its hardware frame-time benefit remains
unmeasured. The installed indexed-vertex comparison is still the next device
test. The smaller executable is not evidence of a particular FPS gain.

## What changes

- Both fresh generations contain 8,021 functions and 928,726 lifted instructions.
  Omitting `--trace-funcs` removes 8,021 entry-marker sites and 35,402 return-marker
  sites. The generated function bodies otherwise match. Relative to the existing
  development source, regeneration also changes one comment and places a native
  hook before local initializers; neither changes guest operations.
- `xv_guest_trace_enabled` changes from 1 to 0. The normal default also stops
  launching the 1 kHz sampling thread. Frame timing, draw preparation, GPU slot
  retirement, upload counters and the FPS display remain available.
- Startup now logs the sampler's off state and whether function tracing is
  compiled in. For this candidate the log reads:

  ```text
  [prof] sampling profiler off; guest function tracing absent
  ```

- Startup also resolves the watch configuration. Without traced entries, its
  lazy `-1` sentinel could persist and cause repeated no-op watch callbacks from
  native helpers. An absent watch list now resolves to zero before normal play;
  explicitly configured lists are preserved.

Setting `XV_PROF=0` in a traced build only stops the sampler thread. It does not
remove the generated entry/return work. Conversely, `XV_PROF=1` can start the
sampler in an untraced build, but that does not restore missing guest markers;
its attribution is incomplete. Use a traced build when diagnosing individual
guest functions or using function watches.

The final native EBOOT is **3,920,396 bytes smaller** than the parity candidate
with tracing. The linked text/read-only section total shrinks by 1,886,470 bytes;
data and BSS sizes are unchanged. EBOOT file-size savings must not be described
as the same number of bytes of freed runtime RAM.

## Actual ARM comparison

The test executes the same functions in separately linked Vita executables.
Across 640 synthetic cases, the entire guest context and 2 MiB guest arena match
byte for byte. Native fast/fallback decisions and modeled firmware-copy counts
also match. The test holds function watches off after normal startup resolution
and excludes the one-time native-math announcement from measured work.

| Routine | Traced instructions | Untraced instructions | Reduction |
| --- | ---: | ---: | ---: |
| Polygon edge helper `0xB77C0` | 419,703 | 417,527 | 0.52% |
| Native clipper directly | 1,081,299 | 1,078,355 | 0.27% |
| Clipper through guest entry `0xB71C0` | 1,085,267 | 1,080,275 | 0.46% |
| Matrix entry `0xB5B40` | 151,235 | 149,187 | 1.35% |
| Quaternion entry `0xB5F60` | 143,588 | 141,540 | 1.43% |

Each row contains 128 cases. All cases execute fewer counted instructions.
Matrix cases include 24 native successes and 104 fallbacks; quaternion cases
include 27 native successes and 101 fallbacks. The deliberately difficult
alignment/alias fixtures do not represent hardware path frequencies. These
counts omit firmware-copy internals, caches, operating-system scheduling and
GPU work; the standalone test does not launch a sampler thread in either mode.
Neither the percentages nor executable size predict game FPS.

## Build and reproduction

Both profiler test variants pass default-on/default-off and explicit override
checks, including the new startup status and empty/configured watch lists. The
full host runtime suite passes. The native build passes, all 81 checked runtime
source/header inputs match source, and all 1,584 non-executable package payloads match
the traced parity package. The diagnostic-marker audit finds no use of the
current-function marker to select gameplay or rendering behavior.

The final executable also passes an isolated emulator check: a normal solo
Blood Gulch start, walking and rifle fire, camera turns, Leave Game, a new Normal
campaign through its first-person cryo start, and Save and Quit back to the main
menu. Four sampled frames cover 1,143 draws with zero changes detected before
GPU completion. The run reports zero vertex-upload failures. This is a bounded
correctness check at the existing 20 FPS emulator cap, not a performance
comparison. Driving, rocket deaths, campaign combat and cutscene skipping were
not exercised in this check. The isolated emulator was stopped after returning
to the main menu; its controlled shutdown is recorded with the local results.

The normal generation command already omits function tracing:

```sh
PYTHON=/path/to/recompiler-python tools/recomp.sh
make RECOMP=1
```

Add `--trace-funcs` to `tools/recomp.sh` when generating an instrumented build.
Regenerate and rebuild in separate checkouts for comparison; these commands
replace generated code in that checkout. The user's game inputs and reviewed
profile are required in both.

With VitaSDK and Unicorn/pyelftools available, compare the actual linked files:

```sh
python tools/test_arm_math_runtime.py \
  --baseline /path/to/traced/xita.elf \
  --candidate /path/to/untraced/xita.elf \
  --functions f_000B77C0 xv_math_polygon_clip f_000B71C0 f_000B5B40 f_000B5F60 \
  --output-dir /tmp/xita-tracing-check
```

The recorded run uses Unicorn 2.1.3. The next acceptance check is physical
throughput at unchanged settings and a matching view, followed separately by
driving, shooting/death and campaign stability. Retain an instrumented recovery
build for diagnosis. This audit does not clear the prior hardware GPU crash.

Local archive: `2026-09-09-085950-trace-overhead` under `xita-backups`.
