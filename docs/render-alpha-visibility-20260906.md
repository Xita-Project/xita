# Rendering and visibility optimization, September 6

The user authorized three steps: attribute expensive submission calls, optimize
rendering work, and reduce visibility-result polling. This candidate implements
all three. The [first hardware follow-up](hardware-20260906-alpha-visibility.md)
records median 9.75 FPS in Blood Gulch and 8.8 FPS in Battle Creek; sustained
20 FPS remains unmet, and uncontrolled routes prevent a causal gain claim.

## Additional hardware collection

Read-only backup:
`/home/birchwoodgod/xita-backups/2026-09-06-112314-render-optimization-hardware/`.
All 65 files were copied and hash-verified. This run still uses the installed
queued-pass build `8c2d56a9...`. There are 47 Blood Gulch world timing windows:
median 9.0 FPS, range 4.6–16.6, median 130 draws per frame, 79.968 ms whole pump,
3.397 ms submission and 69.089 ms final GPU-completion wait. The 27 bracketed
function-profile windows have a mean printed visibility-polling share of 20.63%,
peak 40%. These are sampled wall-time shares, not exclusive CPU costs. Route
differences prevent a controlled comparison with the preceding runs.

## Submission attribution

The existing `[render-time]` format and whole-frame boundaries are preserved.
New `[render-submit]` rows separate elapsed BeginScene, EndScene, vertex-uniform
reservation, fragment-uniform reservation, draw submission and shader lookup.
These are nested subsets of the existing submit timer; they must not be added
to it as independent costs. Internal driver stalls and preemption are included.
No GPU execution-time measurement is claimed.

`[render-work]` reports the eight shader keys with the most submitted indices
in each 60-frame window, including draws and alpha-disabled selections. The
bounded table holds 128 shader keys and reports overflow explicitly. This ranks
submitted geometry volume, not GPU execution cost. Frame-owned counters reset
on the pump thread after the existing completion publication.

## Alpha-disabled fragment shaders

Every table-referenced translated shader previously contained a conditional
alpha-test discard, even when the recorded draw disabled alpha testing.
PowerVR documents the cost of discard for hidden-surface removal, including
the conditional-discard case on SGX. The inference is that specializing these
draws can help the Vita avoid shading hidden pixels; the actual benefit still
needs a hardware comparison. Sources:
[PowerVR discard guidance](https://docs.imgtec.com/starter-guides/powervr-architecture/html/topics/rules/do-not-use-discard.html)
and [SGX conditional-discard explanation](https://forums.imgtec.com/t/cost-of-static-branches-in-sgx-shaders/678).

`tools/specialize_ps_alpha.py` generates 572 `_na.frag.cg` variants from the
existing shader sources. It removes only the runtime alpha-test block and its
uniform. Color/alpha calculations, texture sampling, fog and any texture
CLIPPLANE operations are preserved. The pipeline generates these variants on
future shader rebuilds, and the embedding tool requires their compiled assets.

The draw's captured alpha-test state selects the variant only when testing is
disabled or its function is ALWAYS. All other enabled alpha tests retain their
original program, including foliage cutouts. Both paths retain blend, depth,
texture and vertex state. The bounded shader-link cache keys the specialization
separately; a missing/failed specialized link or full cache falls back to the
original combiner rather than a different effect. `XV_ALPHA_SPECIALIZE=0`
restores the original path after restart. The default is 1.

The existing shader compiler ran in an isolated Vita3K session using the locally
available runtime compiler: all 572 variants compiled with zero failures.
They are embedded alongside the original 572 fragments for executable-only USB
updates. No shader files need to be allocated on the card.

## Visibility polling

An incomplete visibility read now calls the existing cooperative guest-fiber
sleep for a requested 100 microseconds. Other guest fibers remain runnable;
the scheduler can let the host sleep when no guest work is ready. It does not
block the whole guest scheduler with a native sleep. Actual wake latency can
exceed the requested delay because of scheduling and timer resolution.

Ready/invalid queries still return immediately. Pending reads still return
INCOMPLETE without modifying output buffers. Counts remain tied to the issued
generation and are published only after the pump's final GPU completion.
No stale or fabricated visibility results are introduced. `XV_VISIBILITY_POLL_US=0`
restores immediate yielding; explicit delays clamp to 0–1000 microseconds.
`[visibility-poll]` reports reads, pending results and cooperative delays over
guest frame intervals, so the next hardware run can measure polling frequency.

## Validation and candidate

- Submission tests cover disjoint parent totals, nested spans, call counts,
  return-value preservation, single evaluation, a zero clock origin, inactive
  frames, disabled profiling, workload counts and window reset.
- Shader-source tests compare all 572 specializations against the original
  computation, and retain a synthetic texture CLIPPLANE discard. Cache tests
  exercise alpha functions, separate variants, repeated lookup, failed loading
  and a full cache falling back to the original program.
- Visibility HLE tests cover pending output, immediate success/error behavior,
  default/disabled/clamped delays, four-core counts, generations, page-crossing
  writes and 200,000 threaded publication handoffs per run.
- Existing shader, HUD, program-identity, render-target ordering and production
  frame-completion tests pass. AddressSanitizer/UndefinedBehaviorSanitizer pass
  for the cache, visibility and profile tests. The initial sanitizer link retained
  unrelated globals; disabling ASan global instrumentation allowed isolated-link
  garbage collection. That limitation and both build logs are archived.
- Native `make RECOMP=1 -j6` passes in the isolated traced source tree. The
  instrumented guest game code is unchanged.

Candidate archive:
`/home/birchwoodgod/xita-backups/2026-09-06-113248-render-alpha-visibility/`.
The final native SELF is 34,681,186 bytes. Standard compression plus padding
fits the existing 32,918,474-byte device allocation; all three decoded segments
match the native executable. Installed USB SHA-256:
`f2f480ccf8e590f2e02bb77cecb0f0e87cb70faf65772903d40104a6b6a5c752`.

## Isolated emulator validation

The exact final USB executable ran on virtual display `:97` with OpenGL software
rendering. Normal solo split-screen selection started Blood Gulch. Movement,
camera turns and plasma charge/fire worked; close-up foliage retained its cutout
edges and remained visible during firing. Screenshots and the emulator/game logs
are archived under `validation/`.

All 294 complete timing windows have matching submission rows; the largest
printed rounding difference between submission and its parts is 0.002 ms/frame.
All windows retain 60 final completion calls and 60 display queue calls, with
zero intermediate target completion calls. All 295 draw-memory reports record
zero capacity drops. All 45 specialized program loads report `discard-used 0`.
Both original and specialized shader paths were exercised. Every pending read
in the 294 visibility-poll windows used the cooperative delay. No render-target
BeginScene/EndScene failures were logged. The last partially written timing line
at intentional shutdown is excluded from the complete-window count.

Earlier campaign movement, firing, flashlight, pause and save-and-quit validation
used the archived initial candidate, before a final bounds guard in profiler
shader-key lookup. Its identity and logs are retained separately. The final exact
executable was checked in multiplayer as described above.

The existing OpenGL `v_Color0` program-link and unknown texture-format errors also
occur in the baseline and remain unresolved. The known flashlight color artifact
remains open. These checks establish functionality within those limits; they do
not establish pixel equivalence or measure Vita GPU performance.

## USB deployment

Installed September 6 at 11:50 CDT; fresh read-only mount verification completed
at 11:51 CDT. The update overwrote only the existing executable allocation without
creating, truncating or renaming card files. Direct device readback and remount
hashing match the SHA above. All 657 other checked files remain unchanged,
including saves and the user's 480p, textures 128, mip smoothing off, filter 1 and
rear-touch-off settings. The card was safely unmounted after verification.

This is the latest installed diagnostic build. No new VPK installation is needed.
The first hardware log confirms all three paths and identifies EndScene stalls
alongside the dominant final graphics-completion wait. Continue with controlled
hardware comparisons; emulator frame rates are not a substitute.
