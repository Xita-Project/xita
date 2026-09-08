# September 8 morning progress

Later work and the newly authorized private GitHub pushes are recorded in the
[modular/site follow-up](progress-20260908-modular-site.md). The installation
and pending hardware comparison below remain unchanged.

## Installed and safe to disconnect

The tested weapon/menu/audio and deferred-visibility build was installed over USB
at 07:00 CDT. Direct readback and a read-only remount verified the executable;
1,654 other files were unchanged, including settings and saves. The card was
safely unmounted. No new VPK is required. [Deployment record](hardware-20260908-weapon-menu.md).

## Completed local work

| Work | Status |
| --- | --- |
| Skate3-Mobile and vitaGL research | Reviewed actual source, recorded transferable ideas and target-specific constraints. No upstream code copied. [Review](skate3-optimization-review-20260908.md). |
| Exact index/constant scans | Opt-in ARMv7 NEON path passes native, host, bounds-checked ARM and private gameplay checks. Default off; uninstalled. [Details](draw-scan-20260908.md). |
| Fused vertex snapshot copies | Loads guest bytes once and writes both existing snapshots. Passes native, sanitizer, ARM and private gameplay checks. Default off; uninstalled. [Details](vertex-copy-20260908.md). |
| Fog and full-screen effects | Rebuilt nine mismatched fog outputs, added exact composite routing and embedded matching vertex programs. Native, host and private gameplay checks pass; cinematic color fidelity remains limited by the emulator’s RGB565 support. Uninstalled. [Details](shader-varyings-20260908.md). |
| Release preparation | Documentation updated; the new embedded shader header is ignored and excluded from review exports. Nothing committed, pushed or published. |

GitHub main remains at `4261051c6ee07784370bd21f54e1d331247254d4` from September 4.
The dirty local workspace and the tested build stages remain separately backed up.
The repository must remain private until the existing provenance and clean-build
release gates are resolved.

## Performance and next test

The previous representative hardware capture remains **11.00 FPS at 640×360**.
There is no new measured Vita FPS gain. The two CPU candidates target portions
of roughly 2 ms of index/setup work and 3.93 ms of stream preparation; those
savings alone cannot remove the roughly 40.9 ms needed to reach a 50 ms frame.
The emulator's 20 FPS cap and ARM instruction counts are validation evidence,
not hardware performance measurements.

1. On the installed build, run **L + R + Square** in a stationary gameplay view.
   Leave the camera still while eager/deferred/eager visibility phases complete.
   Keep the standard graphics settings unchanged.
2. Play with camera movement, firing, driving and death/respawn, then reconnect
   USB so the complete log and any crash dump can be collected.
3. Compare frame time and visibility wait together. Then test the CPU and shader
   candidates separately, preserving the baseline settings and build identity.

Stable representative 20 FPS remains the target. The local work does not yet
meet that hardware acceptance gate.
