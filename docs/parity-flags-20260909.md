# Shared guest parity optimization — September 9

The Vita runtime now evaluates the Xbox parity flag with an inline nibble
calculation. The previous expression called ARM's software population-count
routine. This reduces bookkeeping shared by translated integer and floating-point
comparisons. It changes neither floating-point arithmetic nor scheduling.

This candidate is on the private `work/2026-09-09-parity-flags` branch, based on
`11d9647`. It has **not been installed on either Vita**. The installed indexed
vertex comparison and the separate vertex-mask follow-up remain separate.
There is no measured hardware FPS improvement from this change yet.

## Evidence and limits

The last available hardware log still contains the native-bounds comparison at
9.492 / 9.351 / 9.547 FPS. No newer indexed-vertex result was available on
September 9. The device was disconnected during this work.

After the first mesh interval, 85 profiler reports place the polygon clipper
at approximately 7.74% and indexed-draw HLE at 12.63% of reported wall-clock
samples. The percentages are rounded top-40 entries, with omitted entries
counted as zero; they include scheduling effects and are not CPU-cycle shares.
This supports investigating shared translated math alongside draw preparation.

The baseline Vita executable contains 2,212 calls to `__popcountsi2` across
716 functions. The candidate contains none. These are static call sites,
not calls executed per frame. Its native EBOOT is 86,024 bytes smaller.

| Vita-compiled test | Baseline | Candidate | Interpretation |
| --- | ---: | ---: | --- |
| Standalone lazy parity check | 24 instructions | 11 instructions | Includes the real linked population-count helper. |
| Explicit EFLAGS parity check | 8 instructions | 7 instructions | Keeps EFLAGS bit 2 unchanged. |
| `0xB77C0`, 128 synthetic polygon cases | 436,232 instructions | 419,703 instructions | 3.79% fewer in aggregate; no slower cases. |
| Native clipper, 128 synthetic cases | 1,092,221 instructions | 1,081,299 instructions | 1.00% fewer in aggregate; 22 cases are slightly slower. |

The largest instruction increase in those clip cases is 65 instructions
(0.109% in that case). These tests compare whole routines, including register
allocation and branches. They do not model cache costs, firmware-copy internals,
GPU behavior, representative gameplay frequencies or physical frame time.
The standalone percentage must not be presented as a game FPS gain.

## Correctness and build checks

- Host and ASan/UBSan tests each pass 130,592 cases: every low byte, flag kind
  and operand width; upper-bit isolation; explicit flags; x87 unordered/equal/
  ordered comparisons; and an unchanged context.
- Actual Cortex-A9 execution passes 30,720 parity comparisons and 256 complete
  routine comparisons between separately linked baseline/candidate Vita ELFs.
  The latter compare every context byte, the entire 2 MiB guest arena, and
  firmware-copy counts. Fixtures include all x87 TOPs, unaligned data, split
  pages, physical aliases, in-place outputs and capacity limits. Forced
  preemption and nonfinite arithmetic are covered by the host clip tests.
- Existing host runtime, native matrix/quaternion, clipping and bounds tests
  pass. The game build retains the same generated guest code and shader assets.
  All 88 checked native runtime inputs match the candidate source; all 1,584
  non-executable VPK payloads match the baseline package.
- An isolated Vita3K OpenGL run starts Blood Gulch through the normal solo
  menu, renders camera turns and assault-rifle firing, and returns through
  Leave Game. Campaign loads the first-person cryo bay, renders both camera
  directions, and returns through Save and Quit. Four sampled frames check
  1,116 draws with zero buffer changes before GPU completion; upload reports
  contain zero failures. These are bounded emulator checks at a 20 FPS cap,
  not hardware performance, campaign-combat or driving/crash clearance.

Reproduce the source-only checks with `make -C recomp/host test-parity`.
With VitaSDK on PATH and Unicorn/pyelftools installed, run:

```sh
python tools/test_arm_parity.py --output-dir /tmp/xita-parity-check
python tools/test_arm_math_runtime.py \
  --baseline /path/to/baseline/xita.elf \
  --candidate /path/to/candidate/xita.elf \
  --output-dir /tmp/xita-math-check
```

The recorded ARM runs use Unicorn 2.1.3. The full-routine tool models firmware
copies, uses synthetic finite inputs and compares the same functions in both
executables. Its coalesced ELF mappings avoid excessive emulator teardown time.

## Next hardware step

First collect the already installed indexed-vertex off/on/off test, preserving
the current settings. Then compare this CPU candidate against that baseline
with the indexed experiment held fixed. Keep resolution, texture cap, triple
buffering, frame cap, map and camera unchanged. Follow with separate driving,
shooting/death and campaign checks. Retain a change based on physical frame
times and correct gameplay, not emulator FPS or instruction counts alone.

Local archive: `2026-09-09-082207-parity-flags` under `xita-backups`.
