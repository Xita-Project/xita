# Track changed shader constants — September 13, 2026

The guest D3D bridge now compares and copies only the shader-constant rows that
its producer updated. Previously every draw compared the complete 3,072-byte
bank, even when the kernel had changed no constants or only a few registers.
This complements [bulk window capture](constant-window-copy-20260913.md), which
reduces bookkeeping when recording an immutable draw snapshot.

## Ownership and behavior

The kernel's constant setter is the sole production writer of `xd3d_state.vsc`.
It records the union of actual written rows after clipping to the 192-register
bank. Bounds arithmetic uses wide integers; the accepted copy loop stays
32-bit and visits at most 192 rows. Existing non-finite normalization and guest
address arithmetic are retained.

The world, immediate-flare and HUD/composite paths consume the dirty range when
they synchronize constants. An empty range requires no comparison. A different
producer, invalid bounds or an intervening generic/partial UI write forces a
full-bank synchronization. UI texture-coordinate overrides therefore cannot
leak into the next world draw. Byte equality still determines generation
changes, preserving signed zero, NaN payloads and frame snapshot reuse.

All of this runs on the existing guest owner. No pointer to guest vertex data is
retained by a new worker, and GPU upload/retirement behavior is unchanged. Any
future direct writer of `xd3d_state.vsc` must also update its dirty range.

## Validation and measurement limits

* `tools/test_constant_tracking.py`: 8,192 production setter sequences under
  ASan/UBSan, with both comparator-selection paths, dirty unions, owner changes,
  partial UI writes, invalid bounds, empty ranges and generation wrap. It needs
  neither a GPU nor owned shader tables. The full draw-state host test also passes.
* `tools/test_constant_upload.py`: 528 production HLE uploads, checking clipped
  destination rows, exact source addresses, non-finite normalization, dirty
  unions, integer extremes and unsigned address wrap under ASan/UBSan.
* Immediate-flare tests pass with clipping enabled, disabled and bypassed.
* A private Vita3K verifier compared the complete bank after every tracked sync.
  It passed at least 940,000 checks without a mismatch through Blood Gulch,
  movement, menu transitions and the campaign cryo room.

Ten sampled Blood Gulch windows (600 frames) requested 24,181 KiB of constant
comparisons across 76,433 checks. Full-bank comparisons for those same calls
would request 229,299 KiB: **89.45% fewer requested comparison bytes**. This is
not an actual cache-read count, CPU-cycle reduction or FPS improvement. The
private verifier itself adds a full comparison and is excluded from the final
candidate.

The final build renders normal Blood Gulch startup, camera movement and
pause/leave, with zero reported rejected constant draws. Its EBOOT SHA-256 is
`4be7a4e162f15a6c79d8ab75d1d1a6700239164a672108476f360e38b6a006d5`.
The VPK integrity check passes; only EBOOT differs from the preceding package.
Artifact verification caught an initially retained verifier object after a
timestamp-preserving copy. That attempt is preserved separately and is not
counted as final validation. The final ELF and run contain no verifier.

Private evidence is in the phase-followup directory under
`audit/constant-tracking/`. No Vita installation was attempted. Compare hardware
frame times at unchanged settings after storage recovery; the larger scene and
object-update costs and stable 20 FPS remain open.
