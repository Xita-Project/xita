# Indirect HLE lookup experiment — September 14

The optional cache is correct in the tested dispatcher fixtures, but ordinary
Blood Gulch uses too few indirect HLE calls to make this a promising performance
target. It remains excluded from ordinary builds and disabled at runtime.

## Change

The existing 4,096-entry cache resolves guest function targets. Indirect HLE
functions previously repeated the guest-table binary search and HLE-table scan.
`XV_HLE_DISPATCH_CACHE=1` adds a separate 256-entry exact-target cache for
successfully resolved HLE functions. Entries retain native function addresses,
not guest memory pointers. Guest-first and primary/extra-HLE lookup priority,
magic dispatch, tracing and nested callbacks are preserved. Failed resolutions
are never cached. Linked lookup tables are immutable.

Only the serialized guest owner accesses the cache. An explicit override enables
off/on/off comparisons and restores the configured setting afterward. Omission
of the build option removes the lookup changes and counters. This does not
parallelize gameplay.

## Validation

`make -C recomp/host test-hle-dispatch` passes 24,576 complete context/4 KiB arena
comparisons across enabled, disabled, unset, traced and untraced modes. Fixtures
cover nested callbacks, guest/HLE and primary/extra priority, null entries, exact
unaligned targets, collisions, magic handling/fallback, unknown lenient targets,
restoration and saved-log interval/reset behavior. ASan/UBSan also pass.

`tools/test_arm_hle_dispatch.py --output-dir PRIVATE_DIR` compiles the real
dispatcher and callback fixture with VitaSDK and runs 8,192 Cortex-A9 comparisons.
Full context, arena, native FPSCR, caller marker and callback counts agree. Cache
hits reduce the tested path's instruction count; misses add work. Counts are
not CPU cycles. Unicorn models environment queries and rejects unexpected trap
or assertion imports. No game files are required by these tests.

The HTTP benchmark selector is `--kind hle-dispatch`. Existing benchmark tests
cover unavailable builds, admission, cancellation, view rejection and override
restoration; the real HTTP fixture exercises the new selector.

## Gameplay coverage and decision

An isolated Vita3K update boot-confirmed runtime
`fca07f3b5836958ea68ba3e2eeb30490c195571c2aaf5d7e29eac781f363054a`.
Normal menus entered solo Blood Gulch and completed three off/on/off trials.
The emulator remained at its 20 FPS cap; these trials establish integration,
not hardware speed. Representative 60-frame intervals contain about 49,700–50,300
indirect guest calls and only 54–106 indirect HLE calls. Enabled settled windows
reuse those HLE targets, but that is approximately one or two such calls per
frame. Direct HLE calls do not pass through this dispatcher.

This evidence does not justify a hardware performance claim or a default change.
The older dispatcher comment estimating 100,000 indirect calls per frame is not
supported by this measured view. Other scenes and games can differ.

The first diagnostic printed counters only to firmware stdout. The final report
uses the existing saved-log hook when linked, falling back to firmware/host
logging otherwise. Host tests check every reported field before reset.

Private build, emulator log, coverage records and ARM results are preserved under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/hle-dispatch`.
Owned binaries and gameplay captures remain outside Git.
