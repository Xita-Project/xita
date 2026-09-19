# Halo 2 hardware startup: native floating-point mode

The first physical Vita startup log was retrieved through the authenticated
dashboard endpoint after upgrading the dashboard to `0.2.0-diag.3 / aa98d37`.
The saved log identifies Halo 2 runtime `5206ca9`. Image loading, cache setup,
guest scheduling, movie/audio startup, and one display frame completed. The
terminal record is an intentional runtime guard exit, not proof of a GPU fault:

```
[h2/blocked] FP environment ip=000535A2 address=005E5E3C value=83000010 esp=005E5E34
```

The instruction is STMXCSR. The value is the native ARM FPSCR, with DN and FZ
enabled; the existing H2 adapter requires both disabled. The
[Cortex-A9 FPU manual](https://documentation-service.arm.com/static/5e8e13b4fd977155116a35eb)
defines these as default-NaN and flush-to-zero controls. This adapter deliberately
supports only masked, nearest-rounding, gradual-underflow SSE execution.

The Vita scheduler creates a real SCE thread for each guest fiber. Initializing
only the application's main thread would not establish the environment on those
threads. A weak profile entry hook now runs once on each guest thread before its
first translated instruction. H2 supplies the hook and establishes its supported
FP controls with clear exception status, preserving unrelated native bits. It
does not reset the environment on yields, or relax STMXCSR/LDMXCSR guards. Other
profiles have no hook and retain their existing behavior. Native host-only
threads bypass this guest initialization.

Tests reproduce `0x83000010`, exercise other inherited controls, and verify a
default MXCSR round trip without changing guest CPU state. Logging intentionally
clobbers simulated native FP state in the fixture, verifying initialization is
written after diagnostics. Normal and ASan/UBSan runs pass; unsupported controls
set after initialization still fail the existing tests. Hardware retesting is
required to determine whether there are additional startup blockers.

Private hardware evidence: `halo2-hardware-startup/20260919T014901Z` beneath the
September 18 unified-games experiment directory. The boot log was copied before
relaunching Halo 2. No game binary, memory snapshot, or private log is committed.
