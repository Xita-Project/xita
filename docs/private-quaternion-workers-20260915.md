# Private quaternion work on object workers, September 15

The hierarchy batch did not produce a repeatable hardware gain. Its lock
attribution still showed quaternion and matrix helpers competing for the shared
transaction lock. This experiment targets a smaller, proven ownership boundary
inside the existing object workers.

## Ownership boundary

The current private-output path captures shared inputs under the lock and then
releases it for arithmetic. The new path can omit that initial acquisition only
when the actual worker thread owns the exact guest context and all quaternion
input, output, scratch and return-address memory lies in that worker's captured
stack allocation. It rejects changed page mappings, foreign or copied contexts,
shared buffers and enclosing transactions. It also acknowledges owner-service
parking before accessing private data.

The three floating-point constants must retain their original image mapping and
canonical values. They are in Halo CE 3925's non-writable `.rdata` section.
Image/mapping replacement must continue to occur with workers drained; checking
a value is not permission to race a writer. Mutable tag and pose inputs retain
the existing guarded path.

The arithmetic, intermediate precision, guest state, spill writes and fallback
remain unchanged. Each worker records arithmetic counters in its own cache line;
the owner reports/resets them only after joining the batch. Cold configuration
still initializes under the existing lock. The optional shared quaternion cache
always retains its guard and disables this bypass.

## Controls and attribution

Build with `XV_EXPERIMENTAL_OBJECT_JOBS=1 XV_OBJECT_QUAT_EXPERIMENT=1`.
The experiment defaults off. `XV_OBJECT_PRIVATE_QUATERNION=1` enables it at startup;
the remote `object-quat` comparison switches off/on/off and restores the saved
policy after completion, cancellation or loss of first-person control. It changes
no graphics settings or worker counts. Build stamps cover enabling and disabling
the affected object files.

An independent diagnostic build, `XV_OBJECT_QUAT_PROFILE=1`, counts caller sites
and input ownership after the existing private-output admission. A preserved
emulator gameplay capture counted 4,546,637 such calls, of which 1,373,492 had
private inputs. This identifies eligible work, not a hardware timing result.
The hardware candidate excludes that diagnostic collection.

## Validation

Production pthread worker tests exercise 600 callbacks per configuration, full
guest context/private-memory equality, exceptional floats and rounding modes,
foreign threads, nested guards, stack bounds and remapped pages. A rendezvous
requires one worker to finish the actual quaternion helper while another holds
the shared guard. Another requires private work to park for an owner audio
commit. Counter reporting is checked after retirement and after reset.

Forty combinations of worker count, private-output policy, lock fast path,
point bypass, quaternion bypass and timed wait pass, including ThreadSanitizer.
Separate ThreadSanitizer runs check modified/remapped immutable constants and
the shared quaternion cache; builds without either bypass also pass.

Two Vita-linked ARM suites each pass 1,280 cases covering full guest memory,
context, fast/fallback decisions and FPSCR. One exercises the normal guarded
wrapper. The other stubs ownership admission to exercise compiled private
arithmetic; ownership and scheduling are established by the production-worker
host tests, not that stub. Firmware copy imports are modeled. Instruction counts
are not hardware cycles or FPS.

Benchmark tests cover unavailable builds/modes, cancellation, loss of view,
restoration and selection after frame retirement.
This bounded change does not prove general object independence or resolve the
reported combat crashes by itself.

## Physical campaign result

The updater installed and confirmed runtime
`d5d301748ea0981621db35b772486bd9e8a03fe997ef93d1237cf864e593eca5`,
while retaining the previous A-slot executable. Resolution remained 960×544,
with standard graphics, both object workers, lightweight mutexes and private
output math. The new bypass remained off outside its comparison phases.

Three stationary first-person cryobay comparisons each settled 60 and measured
120 frames per arm. FPS off/on/off was 4.684/4.696/4.675,
4.691/4.677/4.673 and 4.646/4.700/4.682. Pooling exact elapsed times gives
4.675 off versus 4.691 on, approximately +0.34% or 0.73 ms/frame. This does not
establish a meaningful, repeatable improvement.

During measured counter windows, 92 quaternion calls per frame bypassed the
lock. Summed quaternion waits across both workers fell from 4.95 to 3.28 ms per
frame, but object-basis waits rose from 0.77 to 2.40 ms. Whole object batches
remained essentially unchanged at 18.88 versus 18.94 ms/frame. These overlapping
worker waits are not additive frame latency: reducing one acquisition moved
contention elsewhere without shortening the complete job batch.

Exact flare-result waits in this view averaged about 40.45 ms/frame in both
arms. The existing query-overlap experiment is the next comparison at this
resolution. Earlier low-resolution campaign results with negligible query waits
do not answer that question. The private quaternion bypass remains off by
default; the validated implementation is retained for future larger scheduling
changes and combined comparisons.
