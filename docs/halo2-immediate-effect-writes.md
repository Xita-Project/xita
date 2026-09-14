# Halo 2: immediate menu effect-data writes

The opt-in Halo 2 sound adapter implements the four verified menu calls to
`SetEffectData` at `37B60D`. Effects 4 through 7 accept exactly offset 32,
eight bytes and flags 0, paired with original callers `191294`, `1912AC`,
`1912C3` and `1912DB`. The public wrapper's 42 bytes are fingerprinted before
generation; other sound methods retain their existing guards. This is not
general deferred effect-data support.

The original-code oracle executes the full public wrapper and immediate
implementation: `383D79` writes the saved effect image and `37E5C6` copies
the words to live GP X-memory. The adapter preserves both writes under the
same mutex used by the real DSP worker. It does not reset the monitor,
effects, filter history, sample cursors or previously computed/queued grains.
Arguments are validated before mutation, including device ownership, mapped
source bytes, physical aliases to owned objects and stack aliases. The public
call removes 24 argument bytes and returns success only after both writes.

The engine accepts two aligned 24-bit words in a complete effect-state range.
Invalid ranges, high bits, mix-buffer destinations and faulted/active engines
are rejected without mutation. Scalar parameters prevent host pointer aliases.
The published guest bank views remain their documented snapshots; normal
`GetEffectData` reads live state under the worker mutex. Deferred flags and
unreviewed caller/parameter layouts still stop explicitly.

## Validation

All 44 host executables and 44 focused Python tests pass. Synthetic public-ABI
tests preserve complete guest CPU/FP state except the documented result/stack
change, verify split-page source reads, and reject invalid callers, arguments,
ownership, aliases and backend failure without resource or guest-memory writes.
Engine tests verify both destinations, unchanged surrounding memory/counters,
and actual interpreter instructions consuming changed parameter values.
The engine and public adapter suites also pass AddressSanitizer/UBSan.

The concurrent real-worker test completes 2,000 pair writes while serialized
queries see only complete pairs. A focused ThreadSanitizer run passes and
closes cleanly. The full worker suite's unrelated intentional DSP-fault case
triggers this system's ThreadSanitizer `can't find longjmp buf` interceptor
failure; that full TSan run is retained as failed, not reported as passing.
The complete normal worker suite, including the fault case, passes.

Private owned-program tests execute eight comparisons, each with 256 real GP
frames. Changing the first parameter from 0 to `400000` changes 16,293–16,294
GP mix-memory samples per effect. With that first parameter retained, changing
the second from 30 to 15 changes 15,889–15,893 samples. Rewriting the original
0,30 pair leaves the complete DSP state fingerprint unchanged. The compared
exported FX scratch samples remain identical in these particular fixtures;
these tests demonstrate original DSP consumption, not changed audible output.
No owned opcodes, coefficient tables or asset bytes are embedded in the tests.

The original x86 oracle also confirms out-of-range index rejection and the
different shadow-only behavior of deferred flag 1. Neither is turned into an
unverified immediate success path. Private evidence is under
`dsp-bringup/effect-write-*`, `audio-host/effect-write-original.*` and
`audio-effect-write/validation-manifest.json`.

## Native replay

Native146 is built from newly generated sources with only `code_105.c`
changing relative to native145's generated C/headers. All 170 dependency
targets were retargeted and verified after the completed build. The package
uses the owned image, existing quad shaders and existing real DSP asset; the
private OpenGL/SPIR-V lab configuration is unchanged from native145.

| Native146 executable | SHA-256 |
|---|---|
| ELF | `1afae3122e79533228e2b7c9dfcee9e6b18002b19d2b58184046303133a755d8` |
| EBOOT | `1972a4c2c9ddb55c9131f0aafe06a510a5e1da241b81aecab877df883a5b78a7` |
| VPK | `d6b86948da2a016ae115b475e55294a49e90fad229932b2c8bf2541893de566d` |

Normal Start at 23:09:09.140 UTC is observed as digital `0010`. All four
original calls complete with the actual input words 0,30. Execution advances
to a new guest trap at `002B7289`, with current generated function `00234DD1`,
return `00234E2F`, and EAX/ECX `82537050`. That new boundary remains strict.
The trace SHA-256 is
`caf28cb83355fdc84b1ae4d6d76f8a807b99e829d102a71f160145c481f567d3`;
the parsed terminal channel snapshot is
`0270470997fd84354e2f5ba1358023d7d249e4947182a5b6985f62c82f7f43a7`.

The original Microsoft Game Studios logo is visibly captured before Start in
`native-146-view/window-intro.png`, SHA-256
`74d146b4ba591cd7b711fdaa04178bfc9c9a0a5a4f74e5410de5b8dfa484b7a7`.
Its first decoded input and rendered output remain byte-identical to native145.
The terminal presented frame 163 is black; no original menu has appeared.
The owned emulator has exited and all native artifacts are archived under
`native-146-artifacts` with `native-milestone-146.json`. Build replay uses
the prior [SPIR-V/fresh-cache procedure](halo2-spirv-intro-replay.md), substituting
`native-146-artifacts/halo2-boot.vpk` and a unique attempt label.

The menu objective remains active until the actual original menu is visibly
verified. Packages embed owned game code/assets and must not be uploaded or
distributed. Generated code, packages, traces, images and owned coefficients
remain private and outside Git.
