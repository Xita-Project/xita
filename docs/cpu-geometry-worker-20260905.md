# September 5: share visible-triangle preparation across CPU cores

The latest hardware profile shows core 2 busy while cores 0 and 1 have headroom.
The existing texture worker has no work in several slow campaign windows. Generic
sort 0x11B5A0 and its small-partition helper account for 10.4% of the last sample
window, but multiple callers use that sort; this is not a measured 10.4% saving.

## Implementation

The HLE for Halo 3925 function 0x53FA0 preserves its register/stack ABI, sorts signed
32-bit triangle numbers in place, and emits the original three 16-bit indices for
each triangle. It uses a private 128 KB scratch copy and page-aware guest reads and
writes. The original generic sort remains the allocation-failure fallback.

`XV_GEOMETRY_WORKER=2` uses native serial sorting below 512 triangles; lists of
512–2047 split between the caller and core 0; lists of 2048 or more can also use
core 1. `1` restricts helpers to core 0; `0` keeps the native serial implementation.
The two persistent workers have slightly lower scheduling priority than their
caller, leaving the core-1 render pump ahead of auxiliary work. No worker executes
guest code, accesses guest memory, changes a render cache or submits GPU commands.

The caller sorts its partition concurrently, joins every submitted worker, merges
the sorted partitions, then writes results back to guest memory. Draw ordering,
triangle membership and the retained frame-index lifetime are preserved. Small
jobs avoid worker overhead. A failed worker startup or dispatch leaves that work
with the caller. A shutdown joins both workers before releasing their handles.

The extra storage is 256 KB scratch plus two 32 KB native stacks. Logs distinguish
serial, two-way and three-way jobs and report worker/guest/join totals. Those
sorting times overlap and exclude the guest copy, index expansion and merge.
They cannot be added together to infer a frame-time saving.

## Validation status

65 comparisons against the original Xbox sort pass, including signed random
values, ordered lists, duplicates and 32,767-element inputs. Another 78 comparisons
check the complete index expansion, input mutation, output bounds, fragmented
guest pages, all integer registers and live condition flags. ASan/UBSan pass.

The actual worker code passes 288 serial/two-way/three-way cases against libc sort,
including threshold boundaries, immediate scratch reuse, eight startup failure
points and shutdown. Its ASan/UBSan run passes. The original Xbox function bodies
are taken from locally generated game code and retained only in ignored host build
artifacts; no original game implementation is added to the repository.

The native build passes, including a link check requiring strong definitions of
the new HLEs. An initial candidate selected the archive’s weak compatibility stub;
linking the geometry HLE object before that archive corrected it. The exact padded
SELF boots and runs Blood Gulch. Live logs confirm the native routine and core-0
worker execute. The observed route uses many small serial lists and occasional
512–600 triangle two-way jobs; it has not exercised the 2048-triangle three-way
threshold. All three decompressed SELF segments match the native executable.

This change was installed on Vita over USB at 22:06 CDT with the variant,
input and [solo Split Screen fixes](split-screen-20260905.md). Direct reads and
an unmount/remount verified the executable and 687 unchanged neighboring files.
The [first hardware follow-up](hardware-20260905-geometry-worker.md) confirms
163 core-0 sort jobs, totaling only 19.97 ms of helper sorting across nine reported
windows. No core-1 sort jobs ran; gameplay still averaged roughly 89% core-2 busy
in the sampled stretch. An overall FPS benefit is not established. Campaign camera skip,
remaining geometry spikes, black glass and flashlight rendering are still open.
