# Native object basis preparation — September 13, 2026

This experiment replaces one bounded section of Halo's object-pose preparation
with a native helper. It constructs the orientation and translation matrices
from the object's axes and position. It is disabled in ordinary builds and has
no measured hardware FPS benefit yet.

The [hardware phase capture](hardware-20260912-guest-phases.md) selected scene
preparation and object updates for further investigation. Subsequent private
emulator instrumentation located work within `0x8DDF0`; its selected self time
includes uninstrumented descendants and is not a physical CPU-cycle measurement.
This replacement addresses only `0x8E166..0x8E292` in that larger routine.

## Behavior and ownership

Both the audited executable identity and the entire 301-byte region must match
before generation adds the hook. The translated instructions remain available
as the fallback. Build with `XV_NATIVE_OBJECT_BASIS=1` and set that same variable
to `1` in the private test configuration to enable it. `XV_NATIVE_MATH=0` also
disables the helper. Regenerate the guest code after changing the hooks.

The native path validates aligned, contiguous mappings and rejects overlapping
input/output ranges, including physical aliases and address wrap. It retains
both 52-byte output matrices, raw position/axis bits, mirrored axes, integer
registers, guest flags and the two x87 stack slots left behind by the original
region. It leaves the scheduling budget unchanged because this region has no
calls or backward edges. Native floating-point traps must be masked.

The ARM helper uses explicit ordered scalar VFP operations and conversion
points. Testing found that GCC can select `VNMLS` even with floating-point
contraction disabled, changing NaN signs. Retaining the individual multiply,
subtract and conversion operations fixes that discrepancy, including the
flush-to-zero and default-NaN cases.

Execution remains on the guest owner. No object pointer survives a call, and
this change introduces neither a worker nor a cross-frame object cache.

## Differential validation

`tools/test_object_basis.py` independently lifts the original region from an
owned executable. No executable bytes or generated reference code are tracked.
It verifies that changing the signature disables the hook and that preprocessing
without the build switch yields the unchanged original translation.

The host tests compare full guest context and the entire 1 MiB test arena across
4,096 cases in four rounding modes for each runtime setting: enabled, unset,
disabled and native math disabled. Eight layout failures and six native FP trap
configurations must decline before modifying guest state. ASan/UBSan covers the
same cases. Host arithmetic NaN payloads are normalized only at arithmetic output
locations; raw copied fields remain exact.

`tools/test_arm_object_basis.py` compiles both paths with VitaSDK and executes
them under Unicorn's Cortex-A9 model. All 1,536 fixtures compare the entire
context, arena and native FPSCR exactly, including NaN payloads, all four rounding
modes, FZ/DN combinations, mirrored objects, crossing pages and alias fallbacks.
The representative accepted fixture falls from 623 to 204 executed instructions,
plus one modeled 100-byte memory operation. The mirrored case falls from 722 to
221 instructions with the same modeled memory operation. These counts exclude
the imported memory routine's body and are **not CPU cycles or predicted FPS**.

Private test evidence is retained in the phase-followup directory under
`audit/object-basis/`. An emulator run must also demonstrate nonzero helper use:
the generated guest contains overlapping function entries, so manually staging
only one occurrence can miss the path normal gameplay executes.

## Emulator check and next measurement

The private Vita3K build renders the dashboard, normal solo Blood Gulch startup
and the campaign cryo-room sequence. Camera turns, walking and pause/leave
respond. Logs demonstrate actual helper use: approximately 125 calls per frame
in the sampled Blood Gulch view and 75 in the cryo room, without layout or FP
fallbacks. Mirrored output is covered by differential tests; those sampled
gameplay windows did not exercise the mirrored branch.

The tested EBOOT SHA-256 is
`0950b07ec9e1b37e802a47937911d1dcf99b47e23794fff70f8d7b79c3519921`.
Its package integrity check passes. The earlier staging attempt covered only
an unused overlapping entry; it is preserved separately and is not counted as
validation of the active optimization. No build was installed on the Vita.

Next compare unchanged graphics settings on hardware with phase instrumentation
disabled. Keep the option experimental unless whole-frame timings justify it.
Stable 20 FPS and substantial additional parallel gameplay work remain open.
