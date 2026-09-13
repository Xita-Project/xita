# Halo 2 initial executable inspection — 2026-09-12

The `halo2_5849` profile validates one locally supplied Halo 2 executable and
permits isolated discovery. It establishes executable identity, not playable
support. No Halo CE hooks, symbol files or generated code were reused. This
pass used source commit `164c824`, Python with `iced-x86==1.21.0`, and
host GCC 16.2.1 (20260810).

The final section records the subsequent full extraction and LOOP emission
fix. The initial discovery results below remain the baseline for comparison.

## Input identity

| Field | Observed value |
| --- | --- |
| Executable | `default.xbe`, 4,861,952 bytes |
| XBE SHA-256 | `03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d` |
| Certificate title / ID | Halo 2 / `0x4D530064` |
| Certificate version / region | `3` / `0x00000007` |
| Header timestamp | 2004-09-28 02:28:32 UTC |
| Base / image size | `0x00010000` / `0x005754C0` |
| Retail entry point | `0x002D0AEE` |
| Kernel thunk / TLS directory | `0x00411520` / `0x00411A30` |
| Sections / kernel imports | 33 / 152 (135 function, 17 data imports) |
| XAPI / kernel libraries | XDK 5849, QFE 1 |
| Graphics library | D3D8LTCG 5849, QFE 1 |

The inspected 7z archive was 2,678,017,867 bytes, SHA-256
`a3ede2fd6547b95c37790a1ebcea647e5eddc1a37459e2782d8b06b591ed3fe1`.
Its disc-image member was 4,721,410,048 bytes. The root directory places
`default.xbe` at sector 29,345, byte offset 60,098,560. Extraction streamed the
volume descriptor, root directory and that executable into a private directory;
the full disc image, maps, movies and other executables were not extracted.
These hashes identify the supplied files; archive-wide CRC and authenticity
checks were not performed.

## Initial validation

- The XBE parser reported no warnings. The revision-specific profile check
  passed and did not create its requested output directory. Separate checks
  rejected an edited executable and stale manifest without creating output.
- The memory-image builder produced 5,723,336 bytes, including its eight-byte
  base/size prefix. Header and all 33 raw section copies matched the XBE.
  Image SHA-256:
  `3649c9e8fb03e88d92edb3a3693879bb0aae9201a6a8335e99e5883bab13e970`.
- `tools/test_game_profiles.py`: 10 tests passed, including revision/manifest
  rejection, adapter isolation and output ownership. `tools/test_xbe_image.py`:
  1 test passed. These tests use synthetic inputs.
- Both generic discovery modes completed. Host C syntax checking of the full
  default discovery output checked 34 translation units; 33 passed and one
  failed on an undefined branch label, described below. No game code ran.

## Discovery results and boundary confidence

| Mode | Candidate functions | Blocks | Emitted instruction instances | Unsupported instances | Decode failures |
| --- | ---: | ---: | ---: | ---: | ---: |
| Generic, `--no-data-roots` | 9,722 | 141,674 | 967,087 | 3,853 | 85 |
| Profile, default data-root scan | 20,942 | 290,262 | 2,090,941 | 7,142 | 184 |
| Private exclusion experiment, `--no-data-roots` | 9,694 | 135,775 | 936,789 | 411 | 10 |

These are discovery/emission counts, not verified functions, unique instruction
addresses or a compatibility percentage. Overlapping candidates duplicate
instructions. Neither baseline found an independently validated game `main`;
the profile keeps `roots` empty and begins at the XBE entry point.

`XON_RD` is marked executable in the XBE, but inspected locations contain
online-service strings. The generic section policy does not classify this
section as data. It contributes 3,442 unsupported instances in the first run
and 5,868 in the default scan. For example, the unsupported `outsb` at
`0x0056AF6B` is an ASCII string byte, not a reviewed instruction boundary.
The private experiment added `XON_RD` and `.data1` to `Image.DATA_SECTIONS`
in memory for one run. It changed no repository code or committed profile.
It isolates this large source of noise; it does not validate all other roots.

Mixed sections also contain data. At `0x004086D4` in `XPP`, the four bytes of
pointer `0x004088E0` decode as `loopne` followed by other instructions. The
default scan promotes this data address into a function. The branch target
`0x0040865E` has no raw executable bytes, yet lowering emits a goto to
`L_0040865E`; no such label is emitted. This makes `code_031.c` fail
`cc -std=gnu11 -fsyntax-only -Irecomp`. Data inside `DSOUND` creates additional
false instructions. Section flags and plausible-entry heuristics alone are
insufficient to establish function boundaries for this image.

## Concrete implementation gaps

Reviewed instruction neighborhoods establish real unsupported SIMD operations:
`cmpss` / `movmskps` at `0x00016AEE` / `0x00016AFF`, `movhlps` at
`0x00023D3E`, and `cvtpi2ps` / `movlhps` in the sequence beginning at
`0x001DD950`. Other observed candidates include packed comparisons/min/max,
MXCSR load/store, packed reciprocal square root, MMX conversions and `bswap`.
Each family needs operand and semantic tests on synthetic programs before
counting it as supported. The remaining count of 411 still contains invalid
boundaries; privileged-looking instructions should not be implemented solely
because they appear in this report.

The default scan emits calls to eight kernel exports without an explicit
`xctx` implementation in the current kernel sources:
`IoBuildSynchronousFsdRequest`, `IoSetIoCompletion`, `IoStartPacket`,
`IoSynchronousDeviceIoControlRequest`, `IoSynchronousFsdRequest`,
`MmCreateKernelStack`, `MmDeleteKernelStack`, and `ObReferenceObjectByName`.
The first, second and third also lack `KERNEL_ARGC` entries. Generated weak
fallbacks cannot establish correct behavior or argument cleanup. Other
unimplemented imports, including crypto exports, may become reachable through
indirect calls; the emitted direct-call list is not complete API coverage.

Of the 17 imported data exports, `data_export_var` and `xk_crypto_export`
provide seven. The other ten are `XeImageFileName`, `PsThreadObjectType`,
`IdexChannelObject`, `ExEventObjectType`, `XboxAlternateSignatureKeys`,
`HalBootSMCVideoMode`, `XePublicKeyData`, `XboxLANKey`, `HalDiskSerialNumber`,
and `HalDiskModelNumber`. Current thunk initialization would replace these
with function-dispatch magic values instead of guest data addresses.

No matching XDK 5849 library symbol set was supplied or inferred. The runs
use zero HLE functions and consequently lift graphics, audio and network
library internals. A Halo 2 port still requires reviewed API identification,
runtime behavior, shader/data handling and hardware validation.

## Next bounded stage

First establish code/data boundaries and emit valid C for synthetic cases
where a loop target lies outside discovered executable bytes. Then identify
the startup chain and selected XDK 5849 API boundaries with provenance-backed
local inputs. Add independently tested instruction semantics and kernel data
imports before attempting a minimal startup trace in an isolated harness.
Keep the Halo CE runtime and build output separate throughout this work.

See the [profile README](../games/halo2_5849/README.md) for reproduction commands.
Executable bytes, manifests, instruction samples, generated C, memory images
and diagnostic exports remain private local artifacts and are not part of
this change.

## Full extraction and LOOP emission follow-up

The complete game tree was subsequently extracted to `~/games/halo2`: 81 files,
four directories and 4,720,758,479 bytes, including 30 map files, movies, media
and the disc's updater executables. The destination did not previously exist.
`7z x` extracted both archive members with successful CRC checks. An independent
XDVDFS directory traversal checked paths, duplicate names and extent bounds
before disc extraction. Every extracted file's size and SHA-256 then matched
the corresponding ISO extent. The final `default.xbe` passed profile validation.
Publication used atomic `renameat2(RENAME_NOREPLACE)`; only the task's temporary
ISO and staging directory were removed. Per-file hashes remain in the private
extraction validation manifest.

The LOOP lowering fix applies the existing JMP/Jcc tail-dispatch policy to
targets outside `fn.blocks`, instead of emitting a goto to an absent label.
Internal LOOP branches retain their previous emission and preemption behavior.
Discovery rules, function candidates, unsupported counts and Halo CE hooks
are unchanged. This removes a C-generation failure; it does not make the
spurious XPP pointer a valid function or establish safe game execution.

`python tools/test_loop_branches.py` compiles and executes synthetic `LOOP`,
`LOOPE` and `LOOPNE` programs with external, forward and backward targets.
Its nine variants cover 54 cases, including both ZF states, ECX wraparound,
fallthrough, unchanged flags and a single return-address cleanup. Before the
fix, all three external-target variants failed C compilation with missing
labels; the internal variants passed. All variants pass with the fix, as do
the existing ten profile tests.

Regenerated default Halo 2 discovery output passes host C syntax checking for
all 34 translation units with zero diagnostics. The complete
`recomp_report.json` matches the pre-fix report exactly. This checks generation
consistency and C syntax only; the game was not linked, executed or deployed.
