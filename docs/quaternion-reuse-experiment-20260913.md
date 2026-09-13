# Quaternion reuse experiment — September 13, 2026

An optional exact-value cache now bypasses repeated calculations in the existing
native Halo 3925 quaternion helper. It is an experiment for measurement, disabled
in ordinary builds and at runtime unless explicitly selected. No hardware FPS
improvement has been established. This does not move gameplay onto another core.

## Why this range

A private caller census of normal solo Blood Gulch identifies object poses
(`0x8DDF0`) and marker transforms (`0xA1EC0`) as substantial sources of remaining
native matrix/quaternion calls. A representative 60-frame interval contains
7,553 root-pose quaternion calls, 10,101 child-pose calls, 6,368 marker calls and
2,460 attachment calls. These are call counts from Vita3K, not Vita timings.

The root/child distinction matters: child world transforms consume completed
parent transforms. Running the whole hierarchy concurrently would change its
dependencies. Exact reuse is a narrower way to avoid some repeated work while
leaving that order intact.

## Contract

The 64-entry cache retains values, never guest pointers. Its equality key contains
all four quaternion words, the three constants read by the helper, and native
floating-point control bits. Different maps, addresses or remappings cannot reuse
a result solely because an old address matches. Existing layout/alignment/alias
guards run before lookup. Collisions and changed inputs calculate normally.

A hit restores the 52-byte result, 24-byte scratch region, six modified x87 stack
slots, guest status/flags and native sticky exception flags. The two untouched
x87 slots retain the current caller's values. Rounding, flush-to-zero and default
NaN controls participate in the key. Enabled native exception traps decline the
cache. A miss temporarily isolates raised native exception flags, calculates
through the existing implementation, and merges the caller's old sticky flags
back. This avoids incorrectly carrying a previous caller's FP status into a hit.

Cache ownership is the serialized guest thread. A future worker implementation
must use a separate ownership/FP-state contract; this mutable cache is not a
shared worker queue.

## Validation and cost

- Host tests compare the original owned lift, current native helper and cache
  across 3,072 fixtures in each of four modes: enabled, unset, disabled and native
  math disabled. ASan/UBSan pass. Six additional checks reject enabled native
  exception traps without changing guest memory, context or native FP state.
- VitaSDK Cortex-A9/Thumb builds pass 1,920 original/current/cache comparisons
  under Unicorn, covering all four rounding modes, flush-to-zero/default-NaN
  combinations, warm calls with changed status/TOP, page splits and aliases.
- Native/cache context and entire guest arena match byte-for-byte, including NaN
  payloads. Original/native comparisons normalize arithmetic NaN payloads only,
  following the existing native helper's equivalence contract.
- With the build option absent, the preprocessed math unit is byte-identical to
  the preceding implementation.

The first lookup design was too expensive even by instruction count and was
revised. In a representative accepted warm fixture, current native code executes
403 ARM instructions and the revised hit executes 364, plus a modeled 32-byte
comparison. A first-use miss executes 684 versus 431 for the current helper,
including initial environment checks. Imported memory routine bodies are modeled
and counted separately. These numbers are neither CPU cycles nor Vita frame
times; avoiding dependent floating-point arithmetic and adding cache traffic
have different hardware costs. An inline-only experiment did not materially
improve the hit path and was not adopted.

The uninstrumented native candidate renders Blood Gulch, responds to walking and
camera turns, and opens the pause menu in isolated Vita3K. A separate instrumented
caller census over 1,200 Blood Gulch frames finds approximately 96.7% reuse in root
poses, 68.7% in child poses, 82.0% in markers and 16.4% in attachment calculations.
The overall cache hits on roughly three quarters of calls. That distribution
does not justify enabling it by default: misses cost extra, and some call sites
offer little reuse. No hardware deployment or performance comparison occurred.

## Reproduction and next decision

Build with `make RECOMP=1 XV_QUAT_CACHE=1` and set `XV_QUAT_CACHE=1` in the
private test configuration. Omitting the build option removes the cache calls and
storage. Omitting the runtime setting keeps the existing calculation path.
`[quat-cache]` reports hits, misses, disabled lookups and unsupported FP modes.

Run `tools/test_quat_cache.py --xbe OWNED_XBE --manifest MANIFEST --output-dir
PRIVATE_DIRECTORY` with the recompiler dependencies installed. Then run
`tools/test_arm_quat_cache.py --reference PRIVATE_DIRECTORY/original.c
--output-dir PRIVATE_ARM_DIRECTORY --cc arm-vita-eabi-gcc` with Unicorn and
pyelftools. Generated owned-code references and diagnostic packages stay outside
Git and releases.

Private evidence is under
`/home/birchwoodgod/xita-backups/2026-09-12-222123-phase-followup/audit/quat-cache`.
The preserved uninstrumented candidate is in `uninstrumented-artifacts`:
EBOOT SHA-256 `b782227b60ab98e4f0e5a94c9b0c3bde5813acfa6d5ad3e179522306bf628f06`.
The baseline model-palette candidate remains separately preserved.

Once hardware storage is reliable, compare the cache off/on/off with matched
scenes and phase timing disabled. Use whole-frame time to decide whether to keep,
restrict or discard it. Larger scene preparation and object-update costs remain
the main investigation; this small cache cannot establish stable 20 FPS by itself.
