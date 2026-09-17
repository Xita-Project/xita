# Passive owner tick/scene elapsed census

`XV_OWNER_PHASE=1 XV_OWNER_PHASE_DEFAULT=1` adds two coarse observations during
ordinary Halo CE gameplay: primary entry FA920 (tick driver) and BCB30 (scene
dispatch). Both build options default to **0** and reject values other than 0/1.
An explicit `XV_OWNER_PHASE` environment/config value overrides the compiled
startup default with the existing `atoi(value) != 0` convention (empty and
nonnumeric mean OFF). Configuration occurs after dashboard settings load and
before pump/guest threads. There is no runtime setter or benchmark selector.

This does not enable `XV_PHASE`, holder/motion tracing, or change object-worker
admission, locks, callbacks, guest instructions, graphics, or other feature
defaults. In particular, the original phase facility is deliberately avoided:
enabled `xv_phase_enabled` makes object-job admission fall back to serial work.

## Reading the three periodic rows

The existing 60-frame report prints one row per selected entry and one status
row. `entries` counts admitted outer calls, `recursive` counts nested calls of
the same selected entry, and `completed` counts outer returns. Calls are **not
assumed once per frame**. Retained generated source has FA920 calls at BD3EF
and BD8A1 (the latter has three emitted copies), and a BCB30 call at BD977.
Those static sites do not establish runtime frequency. Use `entries/frames`
and `completed/frames`; preserve early-return, mode and warmup qualifications.

`elapsed-us` is outer **wall elapsed**, including nested work, callbacks,
blocking, worker joins and cooperative handoffs. It is not CPU self time or
GPU time. Different selected scopes may overlap; same-scope recursion is timed
only once. Do not sum both scopes or add them to already nested join/draw/wait
totals. Independent existing reports also have different reset boundaries.

A report inside an open scope charges only the interval through that report
boundary, then advances its start. `open` is current depth, not an error by
itself. The next report can legitimately have a completion with zero new
entries. First-Present warmup, foreign/worker declines, invalid admissions or
backwards clock, owner rebinds, abandoned open scopes and stale cleanups are
reported explicitly. Windows with abandoned/invalid scopes are incomplete
attribution, not a zero-cost result. The first pre-Present calls are unmeasured.

## Admission and lifetime

The first real guest Present/Swap binds native thread and exact live guest
context. **The Present observer has the same trusted guest-HLE ownership
precondition as its two production callsites; it is not a foreign-thread
registration API.** Actual object workers are rejected before scheduler-state
reads. Begin/end reject native-foreign callers before touching owner counters;
begin and periodic reporting additionally verify the current live context,
fiber, state and absence of an object-job marker. Invalid counters use atomic
operations consistently. Only the serialized guest owner changes scope state.

Cleanup attributes cover C returns, including early returns. Ordinary yields
retain their native stacks. On the Vita backend each guest fiber is a separate
SCE thread (`xk_os_vita.c`, fiber create/switch/park); `xk_thread_create` allocates
a new thread/context, and retained thread objects are not freed/reused. Thread
exit marks state3 and destroys its fiber after switching to the scheduler
(`xk_thread.c`, create/exit/run-until-idle). A replacement presenting context
starts a new observation generation, abandons old open depths, and makes late
cleanup tokens inert. Generation exhaustion declines further observations
rather than reusing an old token value.

There is no production same-context nonlocal restart in this scope. The
separate optional worker-query `setjmp` abort is outside it; XLaunchNewImageA
and HalReturnToFirmware exit the process. C cleanup would not cover a future
same-context longjmp/restart. Such a path must add an explicit observer reset
before being supported; a call that never returns remains visibly open. The
observer does not change game behavior to force cleanup.

## Selective owned generation and build

First copy authored runtime/kernel/build files into the retained private stage.
Do not change its shared generated prototypes or regenerate unrelated units.
Run from the merged source checkout, with that stage's actual symbols:

```sh
python tools/gen_owner_phase_hooks.py \
  --xbe /private/owned/default.xbe \
  --manifest /private/owned/game_manifest.json \
  --symbols /private/stage/local/halo_ce_3925/halo_symbols.json \
  --stage /private/stage \
  --output-dir /private/new-owner-phase-bodies
```

The tool validates owned image/symbol identity, retained roots and HLE mapping,
then compares both original bodies exactly before writing either output. It
rejects Python `-O` and any stage-body drift. Replace only the complete primary
`f_000BCB30` and `f_000FA920` bodies with these private outputs. In the qualified
stage they are in code_017.c and code_022.c. Keep prologues, prototypes, interior
entries, unrelated functions, and existing phase scopes45/47 unchanged.

Add the two flags above to the existing cumulative build command. Feature
transitions rebuild main/UI, xd3d, the observer and the two selected guest
units, with the relevant archives. Startup-default changes rebuild only the
observer object and game archive. Other guest units and shared prototypes are
unaffected. Feature ON fails if the two generated hook markers are absent.
Feature OFF removes the optional observer archive member; retained generated
guards compile away. Preserve every existing candidate flag independently.

## Cost and validation limits

An admitted outer call uses two clock reads. Same-scope recursion adds no clock
reads; a report with any open scopes adds one shared clock read. The status row
counts the actual calls. At exactly one completed call of each kind per frame
the observer would make four clock reads per frame, but that is an example,
not a measured frequency. The existing `[profile-cost]` report contains the
three extra rows' formatting/enqueue cost. Admission, native-ID checks, cleanup,
clock latency and changed register allocation still require hardware overhead
assessment; elapsed observations themselves include observer work. There is
no per-draw/per-child timer or extra wait.

Compiled ON but runtime OFF still pays entry enabled checks, a zero-token
cleanup call and Present/report fast returns. Compiled OFF has no observer
references: actual Vita instruction/relocation comparisons matched retained
originals for main, UI, xd3d and both extracted primary entries. This is not a
whole-linked-runtime identity or hardware timing claim.

`tools/test_owner_phase.py --output-dir /private/new-test-output` exercises the
actual observer under ASan/UBSan in18 fresh processes (absent/0/1 defaults and
explicit environment choices), concurrent owner/native-foreign/object-worker
calls under TSan, recursion, early cleanup, split reports, live-context decline,
handoff/abandonment, backwards clocks and generation exhaustion. It also builds
the real Make graph through eight feature/default transitions using tiny source
stand-ins and real cc/ar, checking exact object ownership, archive removal,
invalid flags and missing hook rejection. No owned game bytes are tracked.

Private evidence: `direct-cluster-query/owner-phase-prototype/`. Vita observer
object has156 bytes `.bss`; extracted primary frames grow8 bytes (BCB30 72→80,
FA920 104→112). Begin/end local frames are24/32 bytes; report is104 bytes, before
callee frames. These are compiler outputs, not cycles. No device, emulator,
runtime benchmark or FPS result is part of this qualification.
