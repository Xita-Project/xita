# Halo 2 graphics timer milestone

Native attempt 12 passes the original driver's PTIMER setup and stops at the
next framebuffer configuration read. There is still no returned graphics device,
rendered frame or Halo 2 menu.

The title-local model now counts a 56-bit PTIMER clock at the configured NVPLL
frequency, scaled by the programmed 16-bit multiplier/divider. It exposes low
27 bits shifted left five and the remaining high 29 bits. A programmed alarm
matches the low counter; its pending bit is sticky even when interrupts are
masked, and writing one acknowledges it. Timer disable resets and freezes the
unit. These behaviors follow the primary
[Envytools PTIMER description](https://envytools.readthedocs.io/en/latest/hw/bus/ptimer.html).

The implementation accepts injected elapsed microseconds and carries source and
ratio fractions between updates. The Vita bus obtains elapsed time from
`sceKernelGetSystemTimeWide`; host tests use deterministic input. Rate writes
preserve the counter and restart the fractional ratio phase. Exact hardware
phase at a rate change is not validated. Invalid ratios, impossible elapsed
intervals, counter writes, and nonzero interrupt enables remain rejected.
No interrupt delivery or GPU completion is synthesized.

Tests compare one second against a million one-microsecond steps, check stopped
and restarted clocks, masked alarms, acknowledgment, low/full counter wrap,
multiple-wrap intervals, reset/gating, and rejection without mutation. Generated
synthetic x86 also reads the timer through the real bus with an injected clock.
All 14 Python regression tests, local cache/register/timer tests and the timer
ASan/UBSan run pass. The incremental four-job Vita build passes.

The native trace observes:

| Instruction | Register write | Value |
| --- | --- | --- |
| `0x3FE1FC` | `0xFD009200`, divider | `0xDE86` |
| `0x3FE206` | `0xFD009210`, multiplier | `0x1DCD` |
| `0x3FE210` | `0xFD009420`, alarm | `0xFFFFFFFF` (stored `0xFFFFFFE0`) |

The next instruction boundary is a rejected read at `0x401D9E` of framebuffer
configuration `0xFD100200`, reason 1. Native time-counter reads have not yet been
observed; their behavior is validated by synthetic tests only. This change is
limited to the Halo 2 model/bus/boot target and their tests; CE paths are unchanged.

The next bounded task is the framebuffer memory configuration and GPU instance
memory/PRAMIN mapping contract. The shared `MmClaimGpuInstanceMemory` returns a
plausible retail upper-bound address (`0x83FF0000`) but leaves its padding output
unwritten and does not reserve/shrink instance pages. That later API still needs
a scoped implementation before Halo 2 can depend on it.

Owned executable bytes, generated C, packages and full traces remain private.
The diagnostic VPK embeds owned game code/image data and must not be uploaded
as a distributable release.

SHA-256 from `private/native-milestone-12.json`:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `04200b954b1090ae51424fc5b5f161838ea0cfcabca5464c204b5e7bea07d89c` |
| EBOOT | `175b629190be1c5eda1b11c3cb8dd49afe1d1b2898ff14861e073f4a0eb29e7c` |
| VPK | `ef0ed46e1ce5c31904647be6f935dea1082f26ec93c704a6baacf99adc00dab5` |
| Native trace | `136a6873440ccf47d820c068d52977a721e95ef478f9a6b2090bfc3d9789409b` |
