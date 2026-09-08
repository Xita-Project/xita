# Draw preparation, native math and waits

The September 6 [360p hardware run](hardware-20260906-quality-settings.md)
still spent a median 115.8 ms between engine Presents, despite a shorter
graphics-completion wait. Its sampled hot paths included matrix/quaternion
helpers, draw/state processing and visibility-result polling. This change
targets that work. Hardware FPS impact remains unmeasured.

## Implementation

- **Native translated math:** Halo 3925 matrix multiply (`0xB5B40`) and
  quaternion-to-transform (`0xB5F60`) use fixed native temporaries instead of
  repeated emulated register/stack operations. Operation order, float rounding
  points, x87 status/TOP, final registers and guest scratch writes are retained;
  contraction is disabled for this object. The recompiler checks the complete
  original function's SHA-256 before inserting either hook. Unaligned,
  cross-page or overlapping input/output/scratch layouts use the original
  lifted function, with host-pointer checks covering guest aliases.
- **Draw-state preparation:** unchanged shader definitions and render-state
  writes no longer dirty the shader. A 64-entry cache retains raw and canonical
  identities for exact shader program bytes. Every hit compares all 168 program
  bytes, including hash collisions; mutable guest addresses are not cache keys.
  Color constants still synchronize when changed. Cold all-zero state explicitly
  initializes the shader identity, and uploads handle split guest pages.
- **Frame waits:** sticky request/completion events replace the 100/200 us
  polling sleeps when event creation succeeds. Acquire/release frame counters
  remain authoritative. Completion is published only after final GPU completion
  and visibility-result publication, before scratch ownership can return to the
  engine. A 10 ms event timeout and polling fallback cover failed notifications
  or unavailable event APIs. Shutdown wakes the pump before joining it.
- **Visibility waits:** repeated incomplete reads back off from 100 us to at
  most 1 ms, cooperatively parking only the calling guest fiber. The result is
  checked again after the wait, allowing completion in the same HLE call.
  Query/frame/thread changes reset retry state. Pending results never fabricate
  pixel counts or publish an older generation.
- **Sleep correctness:** a sleeper with no dispatcher objects must wait for its
  deadline. Previously, a stale `WaitAll` flag from an earlier object wait could
  satisfy an empty object list and wake a subsequent sleep immediately. The
  scheduler now excludes sleeps from object satisfaction; explicit wake kicks
  and actual WaitAll/WaitAny behavior are retained.

These changes do not parallelize AI or physics. They reduce work and polling in
the existing engine/pump arrangement. Final GPU completion is still required.

## Controls and measurement

All four optimization controls default to enabled and need no dashboard change:

| Environment control | Disabled behavior |
| --- | --- |
| `XV_NATIVE_MATH=0` | Original lifted matrix/quaternion functions |
| `XV_PREP_STATE_CACHE=0` | Recompute raw/canonical shader identity on sync |
| `XV_FRAME_EVENTS=0` | Existing frame polling sleeps |
| `XV_VISIBILITY_BACKOFF=0` | Fixed cooperative visibility delay |

`XV_VISIBILITY_POLL_US` retains its 0–1000 us range and 100 us default; zero
yields without an explicit delay. Disabling the identity cache does not undo
unchanged-state detection. Visibility's post-wait recheck and the scheduler
correctness fix remain active.

Every 60 frames, `[native-math]` reports fast/fallback counts and
`[draw-state-cache]` reports adjacent reuse, table reuse and computed identities.
`[visibility-poll]` adds completions after waiting and requested delay time.
Requested sleep time is not measured scheduler delay or GPU execution time.
Existing engine/render stage reports support the next same-route hardware test.

## Validation

- Independently regenerated original lifts are the math reference: 120,000
  comparisons per enabled/disabled run cover random values, infinities, NaNs,
  subnormals, signed zero, all eight x87 TOPs, four host rounding modes, full
  contexts, output/spill bytes and fallback guards. Finite results compare
  bitwise; different NaN payloads are accepted when both results are NaN.
- Production shader sync tests cover 18,000 updates, changing colors, mutable
  uploads, cache collisions, repeated states, initial zero state and split pages.
- Production event helpers pass 100,000 threaded handoffs, counter wraparound,
  two-slot ownership, early/stale wakes, independent bits and API failure/timeout
  cases. The pump completion test retains final GPU completion before reuse,
  including the 20 FPS cap.
- Visibility tests cover bounded/fixed/zero delays and completion during a wait.
  Scheduler tests cover stale WaitAll sleeps, deadline expiry, runnable peers,
  explicit kicks and real WaitAll/WaitAny objects.
- Native math passes ASan/UBSan. The isolated draw/event/scheduler fixtures also
  pass, using `--param asan-globals=0` to avoid retaining unrelated whole-runtime
  globals during section garbage collection; global redzones are therefore not
  covered in those fixtures. Broader draw, frame, shader and runtime checks pass.
- The isolated Vita build uses `make RECOMP=1 -j6`. Matrix native ARM code is
  940 bytes versus 2,712 for the lifted body; quaternion code is 1,304 versus
  4,662 bytes. Code size and host microbenchmarks are not Vita cycle or FPS
  measurements.

## Final executable validation

The exact padded USB executable has SHA-256
`a762b09c502660697900bd9ca38a32a7d26bab7fa1266282614ccbb756cdbb84`.
Its compressed SELF contains the same three decoded segments as the native
build. It starts Blood Gulch through the normal solo Split Screen menu in an
isolated software-rendered Vita3K session, supports left/right camera turns,
walking and assault-rifle firing, and leaves through the pause menu. Captures
show the weapon, terrain, rocks and sky rendering during those checks.

One observed 60-frame Blood Gulch window records 5,040 shader identity lookups:
1,980 adjacent reuses, 2,280 table reuses and 780 computed identities (84.5%
reuse). Matrix calls are 22,170 native / 25,620 fallback; quaternion calls are
25,680 native / 270 fallback. These are operation counts, not measured savings;
the substantial matrix fallback rate is a remaining profiling opportunity.
Three on-demand world frames check 508 draws in total, with zero geometry
changes before GPU completion. Draw-memory reports show zero drops.

Vita3K still reports the previously observed OpenGL `v_Color0` shader-link
diagnostic. The visual checks do not establish complete rendering correctness.
The emulator's 20 FPS cap does not demonstrate 20 FPS on Vita hardware.

The same session loads a10 on Normal, advances through the opening space/ship
scene and skips to the cryo bay with the technician visible. Three traced
campaign frames check 936 draws with zero changes before GPU completion.
The private emulator and display server are stopped after capture, and the
prior private test configuration is restored.

## USB deployment

Installed September 6 at 15:17 CDT, then verified through direct device reads
and a fresh read-only mount. The existing 32,918,474-byte executable allocation
was overwritten in place. All 657 other verified files, including saves and
settings, are unchanged. The Vita is safely unmounted at 15:18 CDT.

The executable, VPK, source snapshot, rollback backup, validation captures/logs
and deployment hashes are archived at
`/home/birchwoodgod/xita-backups/2026-09-06-145031-cpu-preparation/`.
Sustained 20 FPS requires another hardware run. Keep settings fixed for a short
Blood Gulch route including camera turns, assault-rifle fire and driving, then
compare the new math/cache counters and engine/render intervals.

The [first hardware follow-up](hardware-20260906-cpu-preparation.md) confirms
activation and cheaper shader-state preparation per draw. Native math handles
39.5% of matrix and 98.7% of quaternion calls. A mostly fixed 544p/High view
records median 6.9 FPS and 98.475 ms graphics-completion wait; its route differs
from the preceding run, so an overall FPS gain remains unestablished.
