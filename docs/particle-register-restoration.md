# Particle motion register-lowering candidate

September 26, 2026. Hardware AR profiling identified particle update/collision
as a contributor to the firing and cooldown cost; see
[the perf255 investigation](perf255-ar-and-light-ownership.md).

The current perf255 body of `80720` exactly matches the older memory-lowered
regeneration (body SHA-256
`b0b3df91dee9bb7b1796f401de583b569591ae126a537ea27d1ef45f5aace15a`). An earlier x87 conversion report lists this body
as converted, and the perf177 splice report lists it as installed. The current
maintained stage does not contain that conversion. The intervening reason is
not established, and no performance regression is inferred from the history.

Perf256's private candidate restores register lowering for **80720 only**, using
`tools/splice_x87_regs.py --only 80720`. The splice requires equality with the
baseline regeneration, so unrelated manual stage patches are preserved. It
keeps x87 slots/status in local variables between synchronization points and
spills/reloads at guest calls. It changes no particle count, collision algorithm,
rendering setting, scheduling policy or other translated function. Generated
bodies and game inputs stay outside Git.

## Correctness checks

`tools/test_particle_registers.py BASELINE_SHARD REGISTER_SHARD --output DIR`
builds the two extracted bodies against the current runtime headers and the
synthetic fixture in `tools/tests/particle_registers.c`. The fixture compares
all guest memory, the complete xctx, preemption counts and callee observations.
It exercises both vector-helper paths, collision/no-collision replies, optional
outputs, zero time step, different x87 TOP/status values and page-ending vectors.
Callee stand-ins alter scratch registers, flags, x87 slots and output memory
while honoring their stack effects. They are not implementations of real game
collision routines, and these tests do not establish gameplay correctness.

Results: 2,000 host ASan/UBSan cases, 1,000 host thread-page-table/render-view cases,
and 1,000 Cortex-A9-targeted cases on the Pi passed with zero mismatches. These
are overlapping case sets on different configurations, not 4,000 unique inputs.
An intentionally omitted status spill was caught in case 3 by a callee-state
observation, even though final memory and context matched. This checks that the
fixture detects an intermediate-state error a final-state-only test would miss.

Private receipts are under `particle-register-candidate/`: extracted-body hashes,
compile commands, the one-function splice report and mutation result. The Pi
result supports ARM correctness only. A Vita build and ordinary firing test with
phase timers off are required before claiming any frame-time improvement.
