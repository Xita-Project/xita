# September 14: native point transforms

Halo CE's point-transform routine now has a guarded native implementation.
Accepted synthetic Cortex-A9 cases fall from about 505 to 265 instructions
(about 47.5%). This is an instruction-count result, not measured CPU cycles
or a hardware FPS improvement.

## Why this routine

A private call census found about 359 point transforms per frame in one
stationary Blood Gulch view, rising during movement and shooting. The later
benchmark view uses about 1,308 per frame. The routine still performed repeated
x87 stack updates and guest page lookups for three coordinates and a matrix.
The replacement resolves each memory span once and computes the same double
intermediates directly.

The 105-byte original routine must match the Halo 3925 profile's hash before
its hook is emitted. The native path preserves float output rounding, all five
overwritten x87 stack slots, EDX, lazy comparison flags and the return stack
adjustment. Input/output overlap is supported because the original consumes
all three input coordinates before its first store. Matrix/output overlap,
split pages, unaligned spans, invalid x87 TOP values, nonfinite inputs and
unmasked native FP exceptions retain the original translated path.

`XV_NATIVE_POINT_MATH=0` disables this helper. `XV_NATIVE_MATH=0` also disables
it and cannot be bypassed by a benchmark override. Counters in `[native-point]`
report accepted calls and each fallback reason once per reporting window.

## Validation

- **66,816 compiled ARM comparisons** preserve the full guest context, 2 MiB
  guest arena and native FP status exactly. Finite and exceptional fixtures
  cover all 16 rounding/FZ/default-NaN control combinations; deterministic
  random floats cover four controls. Native traps are masked for execution.
- **36,864 host comparisons** pass under ASan/UBSan, including global and
  helper-specific disable, restoration, aliases, split pages, all x87 TOPs
  and rounding modes. Separate host checks reject unmasked FP exceptions.
- The owned-XBE hook guard rejects an altered final byte. A further 1,024
  compiled ARM matrix/quaternion comparisons preserve both results and
  instruction counts from the preceding unroll optimization.
- The native package builds with unchanged launcher, shader and update
  contract files. A real emulator dashboard update changes slot B to A,
  relaunches and confirms the candidate's exact hash and empty staging state.
- Blood Gulch loads through the normal solo menu. Movement, turning and plasma
  pistol fire render in the smoke run. One complete `point-math` off/on/off
  trial passes the unchanged-camera check and restores the configured helper.

The measured enabled windows contain 77,460 native calls and 990 layout
fallbacks per 60 frames: about **98.7% accepted** in that view. Disabled windows
use the original routine. Off/on/off frame rates are 19.965/19.964/19.966 under
the emulator's 20 FPS cap; those numbers do not establish a speedup. Deliberately
exceptional test layouts cost extra guard instructions before falling back.

Runtime: `52a17e35e1fbf173bb963514d22e38a3655dbe6a5c34e061b1af45a52200beda`.
VPK: `fc381c93d1ffc2f5849032b9724275cbad97c7ac297364d096baa94b305fcaa7`.
ELF: `9f013de1f860d7428553f7f40febf94b7b9f732beaf09a4150edb39e67a05f79`.

## Next hardware test

The physical Vita's paired service remains unavailable after the earlier
[unconfirmed restart](hardware-20260914-updater.md). This build is not verified
installed there. When access returns, compare matched scenes with the same
settings, phase logging disabled and enough frame-cap headroom:

```sh
python3 tools/vita_remote.py --config /private/path/remote-client.json benchmark /private/path/point-math-001 --kind point-math --runs 3
```

The network thread only submits the request. The existing drained frame
boundary changes the helper override; other math, workers, rendering and
resolution remain unchanged. Unavailable builds reject the request. Completion,
cancellation and lost first-person control restore the configured default.
Production frame acquisition, benchmark admission/restoration and authenticated
HTTP controls pass their host checks, including sanitizer runs.

Private evidence is under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/point-math`.
Owned source references, packages, logs and captures remain outside Git.
