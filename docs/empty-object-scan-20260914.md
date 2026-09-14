# September 14: native empty-object scan experiment

The finer stationary Blood Gulch diagnostic measures 6.733 ms/frame of selected
self time in object update `0x900E0`, with 16.075 ms/frame inclusive and 1,122
entries over 600 frames. Selected self still includes uninstrumented descendants;
it is not a measurement of empty-entry scans alone. The 600-frame capture has no
dropped or invalid windows. These diagnostic timings do not establish a speedup.

The routine makes three ordered scans of twelve-byte object records. Each skips
records whose two-byte identifier is zero. This experiment batches consecutive
zero identifiers natively, then resumes the original translated loop.

- Every active entry and its callbacks remain in original order.
- A batch stays inside one translated page, contains at most 128 entries and
  leaves the final loop iteration to the original code.
- The helper declines negative/wrapped indices and unusual bounds. It preserves
  the exact consumed back-edge budget and stops before a scheduler handoff.
- It changes no memory and reproduces the affected registers and lazy flags.
  All floating-point state remains untouched.
- Batches shorter than two entries retain the original path. A guest-owner
  flag avoids calling the helper repeatedly while the option is disabled.

The Halo profile verifies all 569 bytes of the containing original function
before inserting hooks at the three audited instruction addresses. Each emitted
continuation copy receives the same hook. Other profiles receive no hooks.

## Build and compare

The experiment is compiled out unless `XV_NATIVE_OBJECT_SCAN=1` is passed to
the build. In that build, the runtime variable of the same name defaults off.
The remote `object-scan` benchmark selects off/on/off at the current graphics
settings and restores the configured value. Missing helpers reject admission.
The option is independent of `XV_NATIVE_MATH`.

`[object-scan]` reports entry counts for each of the three scans and the number
of empty records batched. It reports every 60 presents, not per object.

## Equivalence checks

`tools/test_object_scan.py` independently lifts the original zero-test and
back-edge regions from an owned XBE. It checks the full guest context and arena
against the native helper, validates the function-identity guard and verifies
that compiling the option out leaves the translated body unchanged.

The final helper passes 48,000 host cases with ASan/UBSan: explicit on/off,
unset configuration and environment-enabled configuration. Each enabled run
accepts 4,494 batches covering 218,277 empty entries; disabled runs change no
guest state. Fixtures include nonzero stopping entries, short/long runs,
alignment, split/aliased guest mappings, the top virtual page, signed bounds
and exhausted or nearly exhausted scheduling budgets.

`tools/test_arm_object_scan.py` compiles those original slices and the candidate
with VitaSDK, then executes them as Cortex-A9 instructions. All 12,288 comparisons
preserve the complete context, 4 MiB arena and FPSCR. The accepted 4,617 cases
cover 218,862 entries. Their aggregate instruction counts are 10,054,098 for
the original slices and 2,088,276 for the helper. This synthetic distribution is
not measured gameplay coverage; the counts are not CPU cycles or an FPS gain.

Benchmark admission, cancellation/restoration, absent-helper rejection and real
loopback HTTP tests pass. The game-profile isolation suite also passes.
Full emulator gameplay and physical off/on/off measurements are still pending.

Reference generation requires the matching owned `--xbe`, `--manifest` and
a private `--output-dir`. The ARM runner takes that directory's `original.c`
as `--reference` and its own private `--output-dir`; it requires Unicorn,
pyelftools and VitaSDK. Generated code and linked game artifacts stay outside Git.
Private evidence is under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/object-scan`.
