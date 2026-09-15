# Experimental sphere collision arithmetic

The object-worker follow-up replaces the repeated sphere-to-plane distance
calculation inside Halo 3925's BSP traversal with native scalar arithmetic.
It is gated by `XV_NATIVE_BSP_SPHERE=1` at build time. Ordinary builds retain
the translated block. This is a candidate for hardware comparison, not a
verified frame-rate improvement.

The audit traced `87EA0` through `88110` and its five direct callers. The query
context and the result arrays are already allocated on their caller's guest
stack. Creating another shared scratch pool would not remove their cost.
This finding does not establish independence of surrounding object updates;
the existing transaction guards, worker joins and owner services remain.

The helper at `87ECC..87EE5` reads the plane and point through the existing
paged-memory accessors, retains ordered double intermediates, and updates
the original general register and both affected x87 slots. It has no mutable
global data, allocations or callbacks. NaNs, infinities and unmasked native FP
traps decline before guest mutation; the original instructions remain in the
generated function as the fallback. The hook requires the supported complete
image and a matching hash of the instruction block, and applies to both
emitted copies of the loop calculation.

Validation uses the original instructions extracted locally from the supported
XBE, compiled with VitaSDK, and executed in a Cortex-A9 instruction emulator.
All 1,536 cases match the entire guest context, mapped arena and native FP
status byte for byte. Coverage includes all eight x87 stack positions, all
byte alignments, noncontiguous page crossings, input aliases, finite extremes,
exceptional floats, four rounding modes, flush-to-zero and default-NaN modes.
Version mismatches leave the emitted original unchanged. The emulator does
not retain native trap-enable bits, so trap-enabled behavior is not dynamically
verified by these checks.

An aligned finite fixture takes 138 modeled ARM instructions versus 190 for
the translated block, including the candidate's guard and call wrapper. Some
page-crossing fixtures are slower. These counts exclude modeled memory-copy
bodies and are neither hardware cycles nor game FPS. Representative gameplay
and the hardware comparison are still required.

Run the comparison with the recompiler's Python environment and VitaSDK on
`PATH`:

```sh
python tools/test_arm_bsp_sphere.py \
  --xbe /path/to/default.xbe \
  --manifest /path/to/game_manifest.json \
  --output-dir /path/to/new-private-test-directory
```

The output includes extracted original code; keep it private and outside Git.
The current validation receipt is `sphere-arm-production-flags/result.json` under the
private `validation/engine-restructure-20260914T2300Z` directory.

The experimental package's runtime is
`d100f6ac3248ab32649d84e4f78b450b0233d36216864243008141a82342fa98`.
The linked executable contains both guarded call sites. Its 1,588-member
package changes only the game runtime and boot record, retaining the existing
updater contract. It boots the main menu, loads Pillar of Autumn on Heroic,
plays the cryo exit animation and returns to first-person control in the
isolated emulator. Combat and driving validation of this candidate remain
outstanding. It has not been installed on the physical Vita.
