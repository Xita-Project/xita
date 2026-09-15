# Object-worker timing and contention

The physical rocket-pickup run showed about 19 ms per object-update pass, with
roughly 8–9 ms of elapsed mutex waiting per worker. Several simulation passes
can occur between displayed frames. The old 60-pass reports and 60-frame render
reports had independent windows, so dividing an object report by 60 did not
give its per-render-frame cost.

The experimental worker report now runs beside the renderer's 60-frame report.
It records the actual number of simulation passes, batches and jobs in that
window. A pass can contain multiple capacity-limited batches. Reporting does
not dispatch pending jobs: it refuses to read or reset counters while an object
scope or batch is still active. A later empty window reports zero work.

The joined `batch-us` is elapsed time inside the object batch joins. Lane work
sums overlap that time and each other. Mutex waits also overlap and include
thread scheduling delays and any quiescent service pause after contention
begins. These numbers must not be added together as separate frame costs.

## Locating contended calls

`XV_OBJECT_LOCK_PROFILE=1` records the native return address of each contended
lock acquisition, its count, total elapsed wait and maximum wait. This is on by
default in the dedicated experimental object-worker build; setting it to `0`
disables site attribution. Ordinary builds omit the module.

Each worker owns a fixed table of 32 sites plus one overflow bucket. There is
no allocation and no additional clock read or table lookup for uncontended
locks. Report/reset happens after the worker join. The sites identify the
**waiting caller**, not the function holding the mutex. A frequent waiting math
helper does not establish that its own calculation caused the long hold.

Use the exact candidate's private, unstripped ELF to resolve a logged `pc`.
Physical hardware can relocate the executable: subtract the verified runtime
load bias first. Clear the Thumb bit and subtract two bytes from the return
address to locate the calling instruction, then run:

```sh
arm-vita-eabi-addr2line -f -e candidate.elf NORMALIZED_ADDRESS
```

No generated game function or lock ownership rule changes here. The existing
cache-preload and audio handoffs remain in place. This diagnostic is not itself
a frame-rate optimization, and its overhead has not been measured on hardware.

## Missed owner wake-up found during validation

Repeated host runs exposed a separate handoff deadlock with profiling both on
and off. Captured thread stacks show both workers waiting for service replies
or a quiescence release, with the owner waiting on its notification semaphore.
All workers are parked and a request is pending, but the semaphore is empty.

The owner previously cleared `owner_notice` with a release-only store before
scanning worker states. Release does not order subsequent loads after that
store becomes visible. The owner can therefore read an old executing state,
while the worker publishes a request and sees the old notice value of one. The
worker coalesces its notification, the reset finally becomes visible, and the
owner sleeps with no signal left to consume.

The reset is now a sequentially consistent atomic exchange. This acquires the
coalesced publications before the state scan and orders the reset; a later
publication sees zero and posts another wake. The reset after all workers have
finished and all notifications have been drained remains unchanged, since no
producer is active there. No polling timeout or fabricated reply is added.

## Validation

The production worker fixture checks two simulation passes, six batches and
600 jobs reported in a three-frame window, followed by a zero-work window.
Attempted reporting before scope retirement leaves the totals intact. Tests
cover two workers, one worker and owner-only execution with profiling both on
and off. Site counts and elapsed times reconcile with the aggregate contention
totals. Existing real audio-pump and cache-service tests remain enabled.

The unfixed pool hung on repeated runs with site profiling both enabled and
disabled. The repaired production pool completes 50 repeated two-worker runs
in each mode (100 runs, 60,000 callbacks) using the same fixture. Host,
ASan/UBSan and ThreadSanitizer checks pass. The native package preserves
the updater asset contract; only the runtime and its boot manifest change.
The candidate runtime SHA-256 is
`642bd5d04cc69cf34a52a66d8477923caea707c124b5cfa66a35f0661b543022`.
This exact candidate reaches Blood Gulch through the normal menu in Vita3K,
with both worker lanes and frame-aligned site reports present and no STOP in
the captured log. Emulator timing does not establish a physical speedup.
Physical performance and rocket-pickup stability still require testing.

## First physical comparison

The exact candidate was installed into slot B through Wi-Fi, with slot A
preserved and the runtime hash verified. Original quality settings at 360p and
an uncapped frame rate were retained. In a stationary Blood Gulch view, one
off/on/off object-worker trial measured **10.077 / 5.250 / 9.963 FPS**. All three
camera consistency checks passed and configured settings were restored. This
tests the current experimental mode, including its diagnostics; it does not
isolate the profiler's overhead or represent driving/campaign performance.

Fully active frame windows record roughly 325–343 object passes per 60 rendered
frames and about 104–110 ms of joined batch time per rendered frame. Each lane
records roughly 47–53 ms of elapsed waiting per rendered frame. These waits
overlap; they must not be added to each other or to the joined batch time.

Matrix multiplication and quaternion-to-matrix conversion are the largest
waiting callers in this run, followed by basis and point transforms. A uniform
`0x59000` relocation uniquely maps all nine observed physical return addresses
to the exact ELF's direct calls to `xv_object_math_lock` (24 compiled sites).
Resolving the unadjusted addresses produces incorrect function names. The
emulator had no such relocation in this test.

The next target is independent math preparation with explicitly owned inputs
and outputs, reducing the shared-lock scope. The comparison confirms that the
current whole-object worker mode is slower in this view; it does not justify
removing protection from arbitrary shared game state. Rocket pickup after the
cache handoff fix remains unverified on hardware.
