# September 6 queued render-pass candidate

The new hardware diagnostic log puts the median between-target completion wait
at 85.2 ms per world frame, versus 3.74 ms in pump submission. Looking at the
ground reduces recorded draws and produces a 17.5 FPS window, consistent with
the user's instantaneous 20 FPS observation. Full measurements and limitations
are in [the diagnostic report](performance-diagnostic-20260906.md).

This candidate ends each render-target scene and queues the next scene on the
same GXM context without a CPU `sceGxmFinish` at each transition. It preserves
command order, color/depth stores and loads, initial completion, error cleanup,
and the final completion wait before visibility collection and frame storage
reuse. No draw, effect or geometry is deliberately discarded. Target allocation
and `scenesPerFrame=1` are unchanged. The driver can still stall internally.
`XV_RT_QUEUE=0` restores the original synchronous transitions after restart.

The approach follows the ordinary framebuffer-switch pattern in
[vitaGL's scene management](https://github.com/Rinnegatamante/vitaGL/blob/master/source/gxm.c).
This is an experiment in submission scheduling. The measured GPU work still
has to execute; removing an intermediate wait may move its time into submission
or final completion. The [first hardware comparison](hardware-20260906-queued-render.md)
records median 8.0 FPS versus 6.5 before, with differing play routes. Much of
the wait moves into submission and final completion; sustained 20 FPS and a
causal performance gain remain unproven.

## Validation

- Production pool/scheduling host tests pass in both modes. GPU texture reads
  and writes execute only at final completion in the asynchronous mock. Tests
  exercise repeated target overwrite/read, interleaved UI, depth preservation,
  resource reuse, bounds and cleanup after initial and later scene failures.
- Production pump tests preserve completion before frame storage reuse.
  AddressSanitizer and UndefinedBehaviorSanitizer pass for the revised RT tests.
- Native `make RECOMP=1 -j6` succeeds in the existing isolated traced build.
  Only the render-target replay implementation changes in the executable;
  the instrumented guest source and earlier rendering/CPU fixes are retained.
- The exact USB executable boots and resumes the copied Campaign combat save
  in a separate Vita3K OpenGL/software-rendering session. Camera turns, plasma
  firing and flashlight toggling were exercised. The queued run records 84
  render-stage windows, including 60 world windows, and 85 capacity windows
  with zero command/index/attribute/immediate drops. All reported stage sums
  agree within rounding; every measured frame has final completion and display
  submission, with zero intermediate target Finish calls or RT scene errors.
- A restart of the same executable with `XV_RT_QUEUE=0` restores intermediate
  waits. The purple flashlight lighting seen in the comparison occurs in both
  modes; this existing effect-color issue remains unresolved. Captures are
  comparable views, not a deterministic pixel-equivalence test. Emulator
  timings do not predict real Vita performance.

## Artifact and device test

Archive:
`/home/birchwoodgod/xita-backups/2026-09-06-091640-queued-render-passes/`.
It contains the source snapshot, a patch against the installed diagnostic
baseline, native/compressed/padded executables, ELF/VELF, VPK, logs and comparison
screenshots. The original diagnostic candidate remains archived separately.

The native SELF is 34,099,710 bytes. Standard VitaSDK compression plus padding
fits the existing 32,918,474-byte USB allocation. All three decoded ELF segments
match between native, compressed and padded forms. USB SHA-256:
`8c2d56a9d96796ef3676b860586a9c7083bbb14ace30b6a889bbf5c63d4f1006`.

Installed over USB at 09:25 CDT. Direct device readback and a fresh read-only
mount at 09:26 verify the executable; all 657 other checked files, including
configuration and saves, are unchanged. The device was safely unmounted. The
pre-install backup is the 09:04 hardware collection named in the diagnostic
report. The separate emulator sessions were stopped; the user's running
emulator was left untouched. No VPK installation is needed.

Preserve the existing 480p / textures-128 configuration and profiling for the
first comparison. Play Blood Gulch looking
across the map, firing and driving, with a short ground-view interval. Compare
whole frame times and whole pump times; a zero target-wait counter alone is not
a gain. An ordinary build on the same route should follow once the diagnostic
comparison identifies a useful improvement.
