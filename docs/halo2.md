# Halo 2 bring-up

September 18, 2026: offline executable identification and C translation are working.
Halo 2 has **not booted** in the host harness, Vita3K or on Vita. No Halo 2 VPK
is ready. Halo CE's latest VPK remains awaiting the user's installation/test.

## Identified input

The local `halo2.iso` was extracted with `extract-xiso` into
`local/halo2/disc` (81 files). Inputs, extracted maps and generated code stay
in ignored local storage. The checked-in profile contains metadata only.

| Field | Value |
| --- | --- |
| Profile | `halo2_retail` |
| Title ID | `0x4D530064` |
| Certificate version | `3` |
| Executable timestamp | `2004-09-28 02:28:32 UTC` |
| SHA-256 | `03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d` |
| Entry point | `0x002D0AEE` |
| Image base / size | `0x00010000` / `0x005754C0` |
| Kernel thunk table | `0x00411520` (152 imports) |
| XDK libraries | Build 5849, including `D3D8LTCG` |

XDK build 5849 is library metadata, not a game revision. Other executable hashes
require separate validation. The profile deliberately has no Halo CE hooks,
address overrides or guessed API signatures.

## Completed groundwork

- Profile identity validation passes; the generic recompiler accepts this input.
- Discovery emits 20,942 functions, 290,262 blocks and 2,090,941 decoded
  instructions. It reports 111 referenced kernel exports and zero identified HLE
  functions. These are discovery counts, not verified reachable game code.
- Added `MOVHLPS`, `MOVLHPS`, `MOVMSKPS`, `UNPCKHPS`, `CMPPS`, `CMPSS`,
  `MINPS`, `MAXPS`, `ANDNPS` and `DIVPS` lowering. `UNPCKLPS` now accepts
  memory operands too. Packed memory reads translate page crossings; scalar
  comparisons read four bytes and preserve upper lanes.
- Fixed LOOP-family emission when its destination is outside the current
  function: use guest dispatch instead of an undefined C label.
- Unsupported instruction occurrences fell from 7,142 to 6,378 with identical
  discovery counts. Many remaining decodes look like embedded data, so this
  does not measure execution coverage.
- Native SSE differential tests pass at `-O0` and `-O2`, covering register
  aliases, separate guest pages, all eight comparison predicates, signed zero,
  infinity and NaN operands. Division is checked with finite normal operands.
  MXCSR exception flags, guest rounding modes and denormal modes remain outside
  this implementation's guarantees.
- All ten profile tests and the default host runtime regression suite pass.
- All 34 regenerated C translation units pass host C syntax checking. Before
  the LOOP fix, one failed with an undefined branch label.

## Reproduce locally

From the repository root, with `iced-x86` installed in `.venv` and the owned
executable already extracted:

```sh
.venv/bin/python -m recompiler local/halo2/disc/default.xbe --profile halo2_retail --check-profile
.venv/bin/python -m recompiler local/halo2/disc/default.xbe --profile halo2_retail -o local/halo2/recomp
.venv/bin/python tools/check_generated_c.py local/halo2/recomp --report local/halo2/syntax-report.json
.venv/bin/python tools/test_sse_lowering.py
.venv/bin/python tools/test_game_profiles.py
make -C recomp/host test
```

The syntax checker validates emitted C without linking or executing it. It
returns failure for compiler errors or an empty output directory. Native SSE
tests require an x86 host and a C compiler; no game assets are needed.

Private evidence: `local/halo2/game_manifest.json`, `recompile.log`,
`baseline-report.json`, `recomp/recomp_report.json`, and `syntax-report.json`.

## Next milestones

1. Audit discovery from the entry point and real call chains. The current
   data-pointer scan queues 19,290 candidates and reports 184 decode failures;
   frequent `DAS`, port I/O and `ARPL` decodes suggest data being lifted as code.
2. Identify Halo 2's XDK 5849/LTCG XAPI, graphics and audio entry points and
   calling conventions. The current profile identifies no HLE APIs.
3. Build a Halo 2 host adapter and trace startup through kernel initialization,
   file access and thread creation. Common HLE still contains Halo CE-specific
   behavior and addresses; audit those before reuse.
4. Implement and validate the Halo 2 graphics/resource/shader path, then add a
   native `runtime.mk` and package target. The CE runtime is not a validated
   Halo 2 adapter.
5. Reach the menu, then `00a_introduction` / `01a_tutorial`, in a repeatable host
   or emulator run before asking for Vita testing. Track correctness and
   performance separately.
