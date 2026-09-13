# Selected guest-phase timing — September 12, 2026

This diagnostic candidate times 32 reviewed Halo CE entry points around the
main loop, tick dispatch, camera update, view setup and scene preparation. It
is the next investigation after the [vertex worker hardware result](hardware-20260912-vertex-worker-results.md):
the worker saved about 2.14 ms in one comparison, while that scene still needs
about 36 ms/frame removed to reach 20 FPS. This change measures larger batches;
it does not itself move more game logic onto other cores or establish an FPS gain.

## Enabling the diagnostic

Use the supported `halo_ce_3925` profile and append `--phase-timing` to the
normal recompiler command, then rebuild the native application. The revision
guard must match the owned executable. Other adapters must explicitly provide
reviewed targets before this option is accepted. The selected addresses and
labels live in `games/halo_ce_3925/hooks.py`, not in the generic lifter.

Set `XV_PHASE_TIMING=1` in `ux0:data/xita/xita.cfg` and restart the application.
Keep log saving enabled with `XV_SAVE_LOG=1`. The startup record should read
`[guest-phase] on; 32 compiled scopes`. A regular build without the generation
option cannot enable these scopes just by changing the setting.

Normal generation contains no scope sites. The diagnostic's runtime switch
defaults off; off makes no phase clock reads. A diagnostic executable still
contains the selected scope guards when switched off, so use the ordinary
generation for final performance comparisons. This is a developer diagnostic,
not an additional dashboard graphics option.

Play a few minutes using the current settings, including an expensive view
and ordinary movement or combat. Capture the current `xita.log` and rotated
logs before another session replaces them. No special benchmark keybind is
needed for this investigation. Record which map and activity correspond to
the capture; different maps, views and settings cannot establish a speedup.

Analyze completed reports locally with:

```sh
python tools/analyze_guest_phases.py path/to/xita.log --last 5
```

`--last 0` selects all complete windows. The analyzer rejects windows with
dropped/invalid accounting, malformed records or duplicate phase rows, and
reports incomplete windows instead of silently treating them as valid.

## Reading the measurements

Reports cover 60 guest Present/Swap calls. `end-frame` correlates them with
other frame-numbered diagnostics. Row values are window totals in microseconds;
the analyzer divides by frames and ranks milliseconds/frame of selected self
time.

| Field | Meaning |
| --- | --- |
| `active-us` | Inclusive elapsed time while this guest owns the scheduler baton |
| `self-us` | Active time excluding other selected nested scopes |
| `parked-us` | Inclusive time across explicit cooperative guest handoffs |
| `parked-self-us` | Parked time attributed only to the innermost selected scope |
| `calls` | Scope entries during this window; an open scope can report time with zero new entries |
| `report-us` | Measured bulk report cost, excluding the final cost record itself |

**Active is elapsed time, not CPU cycles.** It includes native blocking calls,
GPU/worker waits within those calls and host preemption while the guest retains
the baton. Parked time measures a cooperative handoff; it is not proof that
another core was busy throughout. Inclusive parent/child rows overlap and
must not be summed. Selected self time still contains unselected callees.
Compare it with existing draw preparation, submission, completion and core
utilization logs before assigning a cause.

Names such as `tick_108A10` deliberately retain their addresses. They are
call-chain boundaries, not claims that an entire function is AI, physics or
animation. Once hardware identifies a large region, inspect its reads,
writes and callbacks before choosing a batch to parallelize.

## Accounting and validation

The serialized guest baton owns the accounting structures. Scope cleanup
covers ordinary returns, early native helper exits and tail-dispatched calls.
Actual guest handoffs suspend and resume the current context; a same-thread
yield fast path does not park it. Thread termination forgets its live scopes
before stack destruction. Open scopes are checkpointed at report boundaries.
Limits are 48 targets, 32 active contexts and depth 32; overflow is reported.

Synthetic host tests cover nested and recursive scopes, parked contexts,
window splitting, early-return cleanup, overflow, termination/context reuse,
stale cleanup, invalid ordering and a backward clock. AddressSanitizer and
UndefinedBehaviorSanitizer pass. Generator tests check opt-in scope placement,
default output preservation, revision/target guards and generated C syntax.
Scheduler tests exercise real handoff and exit call sites with mock fibers.

Private Halo CE regeneration contains exactly 32 sites. Removing just those
lines makes all generated guest bodies match the preceding worker build;
discovery and imports are unchanged and broad function tracing stays off.
Existing host runtime, completion and CPU-preparation suites pass.

The VitaSDK native build and VPK CRC check pass. The EBOOT is 31,118,526 bytes,
SHA-256 `44a1912c972c78bd87856ac7437b5ce99139261c29f32dcc5c266bd6496cf961`.
All 1,584 other package entries match the preceding installed vertex-worker
package, so this candidate requires only an executable update.

An isolated Vita3K run reaches the dashboard, Halo menu and `a10` cryo-bay
first-person sequence, accepts camera input and opens the pause menu. Live
reports preserve the open main-loop scope across windows and distinguish
nested self time from parked time. The captured last five campaign windows
contain 300 frames, with zero dropped or invalid records and no parser errors.
The saved enabled session contains 105 complete, usable windows (6,300 frames);
the analyzer correctly excludes one incomplete final window from emulator
shutdown. Restarting this same executable with the setting off reaches the
menu and emits only the startup `off` record, with no phase windows. Both
isolated emulator sessions were stopped by the operator; the enabled session
returned to the menu through Save and Quit first.
Private build, test and capture artifacts are under
`/home/birchwoodgod/xita-backups/2026-09-12-212616-guest-phase-timing`.

## Test Vita update

Source `30deb24` was installed over USB on September 12. The preceding
executable, configuration and logs were copied and hash-verified first.
The installed EBOOT matches the candidate hash above, including a second
readback after USB unmount/remount. `XV_PHASE_TIMING=1` was appended to
`xita.cfg`; all previous configuration bytes were preserved. Its resulting
SHA-256 is `618503cc2eebb67641ed4565ff106bfd2113428ddab9353354cf7349d07b741c`.
USB was safely unmounted after verification. No VPK reinstall was required.

Hardware attribution and any subsequent optimization remain pending. Do not
treat emulator timings or instrumentation tests as evidence of Vita FPS gains.
