# Legacy packed SSE minimum and maximum

Halo 2 native 164 reaches `MINPS xmm0,xmm3` at `2E1E1A`, immediately followed
by `MAXPS xmm0,xmm4` at `2E1E1D`, in original function `2E1D10`. Discovery
already reaches the routine; these instructions previously stopped as
unsupported. The change lowers these two legacy SSE operations only.
Scalar `MINSS`/`MAXSS` and all other instruction paths remain unchanged.

Intel SDM Vol. 2B, MINPS/MAXPS, specifies selection of the second source
when either operand is a NaN or both are zeros. The selected bits must remain
unchanged, including signaling-NaN payloads and the sign of zero. Both
instructions can raise invalid for quiet or signaling NaNs, and denormal
for subnormal operands. Native x86 checks establish the per-lane exception
priority: a NaN suppresses the denormal flag in that lane; another lane can
still accumulate denormal. Reference: [Intel SDM, revision 089](https://cdrdv2-public.intel.com/868137/325462-089-sdm-vol-1-2abcd-3abcd-4.pdf).

The Cortex-A9 helper classifies and orders IEEE-754 binary32 bit patterns,
selects exact operand bits, and accumulates the corresponding FPSCR IOC/IDC
status. It preserves unrelated FP status and controls, EFLAGS, all other
registers and guest memory. Source and destination arrays are copied before
writeback, including same-register operands. Memory inputs use the existing
complete 16-byte guest read helper and its current alignment/fault policy;
this change does not introduce architectural alignment exceptions.

The ARM implementation requires masked exceptions, DN/FZ disabled, and zero
vector Len/Stride controls. Unsupported controls return to an explicit
instruction stop before destination or FP-state mutation. Rounding modes
are immaterial to bit selection and all four are tested. Halo 2's existing
checked LDMXCSR adapter already enforces a subset of these controls; this
change does not add a separate guest MXCSR model. On SSE-capable x86 hosts,
the helper executes the native legacy instruction, retaining native control
and exception behavior. Other host architectures reject this operation.

Validation uses synthetic values and an independent native x86 oracle:

- 202,048 packed integer-selection/exception comparisons, including a full
  32 by 32 special-value matrix and random binary32 values.
- 73,728 emitted cases covering all XMM source/destination combinations,
  same-register operands, memory sources including page crossings, full
  guest context, untouched memory, and native MXCSR rounding/DAZ/FTZ/status.
- The same host cases pass ASan/UBSan.
- 18,432 compiled Cortex-A9 full-context/memory/FPSCR comparisons against
  native x86 output and status, plus 1,008 strict-control rejection cases.
  Another 864 exception-enable probes read back as zero in Cortex-A9/Unicorn
  and are excluded from rejection coverage, rather than counted as passes.
- All 44 Halo 2 host executables and 55 focused callback/profile/branch and
  prior packed-decode tests pass. Prior RSQRTPS accuracy checks are unchanged.

Run `python -m unittest tools.test_sse_minmax`; set `XITA_TEST_SANITIZE=1`
for sanitizers. Run `python tools/test_arm_sse_minmax.py --output-dir <private-dir>`
with VitaSDK, Unicorn and pyelftools for the Cortex-A9 checks. No game assets
are needed by these tests. The actual game's native 165 build/evidence stays
in private `packed-minmax`; any diagnostic package embeds owned content and
must not be distributed.

Native 165 verifies all 171 dependency targets and executes past the original
`MINPS`/`MAXPS` boundary. It reaches a later checked callback stop at `31AAC0`,
called by `316C90` through offset `14h`, return `316CD5`, object `014E3030`.
The target is an original six-byte constant getter; it remains a separate
callback-discovery task. No callback is fabricated by the instruction change.

The original Microsoft Game Studios intro is visibly present in
`native-165-view/early-movie-middle.png`, SHA-256
`8d018af8d4ecf6963da7bfe34321915eb9596e30d976f850acd4055aeb32b8ad`.
The map copy completes before normal Start. Transition capture lasts 40.8
seconds before the controlled emulator stop. Final frame 135 remains black
and the decoded channel is unchanged. No original main menu is visible.

| Native 165 artifact | SHA-256 |
| --- | --- |
| ELF | `c443d563885a00123934c7c84ef74e1099a970f247c5bc41e13ddc6c0ec942ba` |
| EBOOT | `a0e70bcc80c7eaad67dd0f9c187a7fbc12909dd402df3d571851b229e24e3f46` |
| Boot trace | `4fae4c7912f40349ad2be6a9f3ad99e387a5e8deaf65bd997ddee6ef4f346e1e` |

Private replay from the private base, with only this lab stopped:
`python3 preserve_fresh_cache.py native165-replay`, then
`python3 capture_run.py 165-replay native-165-artifacts`, then
`python3 drive_startup.py 165-replay native-165-artifacts`. The archived package
contains owned content and must not be distributed.
