# Halo 2 host miniport shutdown

Native attempt 35 completes the initial device's intended release, then runs
application memory/cache initialization and creates a worker thread. The worker
stops at undiscovered entry `0x00120C30`, passed by the original instruction at
`0x00120B0E`. No main menu or game geometry appears; the only presented frame is
the previously validated black initialization buffer.

The opt-in host-channel backend now pairs its initialization with a pinned
replacement for miniport shutdown `0x003FE4CB` (436 bytes, SHA-256
`2d6efe5e6fab632c7e291a17ec657f774d20f7952bcbf759278e62df7995c214`).
The original device destructor and its allocation/free operations still run.
The replacement requires the known miniport, fully mapped state and instance
memory, completed synchronous submission, an already blanked display, no queued
flips, no client callbacks, and the supported single-channel configuration.
It records final logical RAMFC cursors, disables canonical tile mappings, and
retires the host command state. A subsequent device can initialize fresh state.

This implements the lifecycle of the host consumer. It does not serialize an
NV2A hardware context, register/disconnect real Xbox interrupts, or emulate the
physical FIFO shutdown registers. Those hardware services were not installed by
the paired host initializer. Guest allocation ownership remains unchanged.
The replacement is void, preserves CPU/FP/control state, and removes its return
address; it supplies no fabricated successful device result.

Validation: all 13 host executables and 16 Python regressions pass. Runtime
AddressSanitizer and UndefinedBehaviorSanitizer pass. Tests check invalid state
and mappings before mutation, active display, pending callback, double shutdown,
complete CPU-state preservation, retired channel rejection and fresh miniport
initialization. Native attempt 35 records idle PUT=GET `0x03C2B000` at shutdown.

| Native attempt 35 artifact | SHA-256 |
| --- | --- |
| ELF | `783902fbf7272ed7349a76cbb409a010a5ede128c62d6cb775d1a4b4c1577a1a` |
| EBOOT | `cd980d7bf6d1c3a243cc3b064deee2917f84f66716666641b9bf7da1205edf3a` |
| boot.log | `a34bfc936afef1fd5e0f5de94b4198b3435f94a332132b6f7d6617a988625634` |

The device snapshot at the worker trap is fresh. There is no current push-ring
snapshot after the device has been cleared; a prior file is not evidence for
this stop. Game-derived artifacts and diagnostic packages remain private and
must not be distributed. Next: discover the evidenced worker entry and trace its
actual startup work toward the application's next graphics device.
