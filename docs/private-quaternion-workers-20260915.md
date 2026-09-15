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
restoration and selection after frame retirement. Hardware results are pending.
This bounded change does not prove general object independence or resolve the
reported combat crashes by itself.
