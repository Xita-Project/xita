# September 14: unroll native transform math

The native matrix/quaternion unit now builds with `-O3 -funroll-loops`, retaining
`-ffp-contract=off`. In accepted synthetic Cortex-A9 cases, matrix execution
falls from about 585 to 446 instructions and quaternion execution from 425 to
401. These are instruction counts, not CPU cycles or a measured FPS gain.
Physical gameplay performance remains unmeasured.

## Change and correctness

The first compiler-only prototype changed arithmetic NaN sign/payload bits.
The accepted implementation explicitly retains the preceding native helper's
scalar VFP operand order, including translation accumulation and the scale
product. The fixed loops can then unroll without changing the tested results.
The math unit grows from 3,200 to 3,679 text bytes with the current VitaSDK.
Other translation units retain their existing compiler options.

The compiled ARM baseline and candidate pass **48,896** full guest-context,
guest-memory and native floating-point-status comparisons:

- 40,960 exceptional-float cases across all 16 rounding/FZ/default-NaN control
  combinations, with native exception traps masked.
- 6,144 deterministic random-float cases across four native FP controls.
- 1,792 finite transform cases across seven controls.

The fixtures include accepted in-place/physical aliases and rejected alignment,
partial-overlap and page-crossing layouts. Both versions take the same native
or fallback path. NaN payloads are compared exactly between these ARM builds.
Firmware memory-copy bodies are modeled; their calls and bytes are reported
separately. Every tested path uses fewer instructions, but the deliberately
frequent fallback cases reduce the aggregate percentage substantially.

Independent owned-XBE hook guards pass. Host ASan/UBSan runs also pass 120,000
original/native comparisons and 120,000 disabled-helper comparisons, covering
all x87 TOP values, float edges, context, spill/output bytes and decline guards.
The existing original/native host contract normalizes arithmetic NaN payloads;
the ARM baseline/candidate checks above do not.

## Validation and next measurement

The native package builds with the same launcher, shaders and update contract.
A real loopback update from the emulator dashboard uploads the runtime, changes
slot B to A, relaunches and confirms the exact hash with empty staging state.
Halo's main menu, normal solo-match setup, Blood Gulch gameplay and the pause
menu render and respond to input. A smoke run walks, turns and fires the plasma
pistol; captures show the world, weapon, projectile/impact effects and HUD.
Gameplay logs confirm about 40,962–42,773 native matrix and 25,868–26,978 native
quaternion calls per 60-frame window. This checks integration, not hardware speed.

Runtime: `015f63991b77f9c687a9f26e76873faa347e3dfe3bea20ebee3430c1d6334f3c`.
VPK: `e0e0add363cb1f5ce2b94d82c9ab5b88a8dd376760da2d2b71acfa9e7ee76e29`.
ELF: `bdf71ad1f5d3421bc60e57f38ffc8df3ba7fd09669c653bfd93e7894cded4f17`.

The paired service on the physical Vita is still unavailable after the earlier
[unconfirmed updater restart](hardware-20260914-updater.md). This candidate is
not verified installed there. Measure matched hardware scenes with phase
logging disabled when access returns; keep the preceding package for comparison.
[Arm's Cortex-A9 optimization discussion](https://developer.arm.com/community/arm-community-blogs/b/operating-systems-blog/posts/arm-neon-optimization)
also illustrates why instruction dependencies can outweigh a smaller count.

Private evidence is under
`2026-09-13-worker-sizing/validation/hardware-updater-20260914T122650Z/math-unroll`.
`tools/test_arm_math_runtime.py` accepts `--float-edges`, `--random-floats`,
`--cases` and `--fpscr` for reproducing the compiled comparisons. Owned source
references, executables and gameplay captures remain outside Git.
