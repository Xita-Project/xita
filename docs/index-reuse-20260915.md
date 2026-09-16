# Reuse exact retained indices within a frame

This opt-in prototype targets repeated index copies and coverage construction
during draw preparation. It does not change vertex values, triangles, draw order
or the existing GPU completion rules. `XV_INDEX_REUSE=1` enables it; the default
remains off until a physical comparison establishes a benefit.

A bounded CPU cache holds 64 entries for draws with 64–4096 indices. Source
pointer and count select an entry, then exact byte comparison with its cached
snapshot decides whether reuse is valid. The GPU index pointer, vertex bound
and reference mask can be reused only when those bytes and the reference policy
match. A changed source, hash collision or changed count/policy creates a new
version through the existing append-only GPU index ring.

On a miss, the guest indices are captured into a cached mirror first. Both the
GPU bytes and their bounds/coverage derive from that one version. No second
read of mutable guest indices or readback from uncached GPU storage establishes
the mask. Replacing the CPU entry cannot change earlier GPU draws. Each new
recording frame invalidates the cache, including the standalone swap path.

The CPU mirror is recording-owner scratch, allocated lazily, and is never read
by the GPU. GPU indices still reside in their original frame slots and retire
through the existing fences. A failed cache allocation or an ineligible size
retains ordinary copying. A cache hit can still use an existing valid allocation
when the append pool is full. Shutdown frees the CPU cache after worker shutdown.

The `[index-reuse]` report counts hits, eligible misses (initially labeled
`rebuilt`), ineligible sizes, comparison-range bytes and avoided GPU-copy bytes.
A comparison can exit before reading its whole range; a miss can fail allocation.
These counts do not measure FPS:
extra capture work on cache misses can offset savings from hits.

Production-retainer tests pass under ASan/UBSan with the option off, on, and
with forced allocation failure. Cases include same-pointer rewrites, odd source
addresses, full rings, direct-map collisions, policy changes, full 16-bit index
values, and 60 modeled slot generations with two older GPU generations retained.
The existing indexed-vertex tests also pass, including 4000 draws over 500 slot
generations. These host lifetime models are not physical GPU tests.

The native build passes package verification: the private candidate changes only
`game-a.self` and its matching `boot-game.txt`. Its runtime digest is
`8785a7e301ad8ececc351b6fd2ba54f06c838dbe6caaf84d3af767a086985d27`.

The owned CE emulator boots this exact candidate with reuse enabled and enters
Blood Gulch through the solo split-screen menus. A stationary 60-frame window
records 1860 reuse hits and 1325 KiB of avoided GPU copies. Turning the camera
and firing a charged plasma shot continue to render. This is a startup and
limited gameplay smoke test, not full correctness coverage or a Vita FPS result.
The emulator remains capped at 20 FPS; that cap is not evidence of a hardware gain.

That initial prototype left index reuse off on the physical Vita while measuring
periodic profiling cost. The subsequent controlled hardware result is below.

## Controlled comparison

The remote `index-reuse` benchmark now compares off/on/off in the same session
without changing resolution or other graphics settings. Each arm settles for
60 frames and measures 120. Transitions run on the recording owner after the
existing submission drain. They invalidate CPU lookup metadata and preserve
already retained GPU indices. Completion, cancellation and loss of first-person
control restore the configured setting, including a configured-on baseline.

Production-retainer tests exercise repeated on/off/restored transitions with
two older frame slots retained, source mutation, allocation failure and existing
cache entries. ASan/UBSan passes. The benchmark state machine, drained dispatcher
and remote client/server selector tests cover the new mode, including builds
without its implementation. An allocation failure still falls back to ordinary
retention; a physical result must show enabled reuse and actual cache hits before
being interpreted as a comparison of the optimization.

## Physical result — September 15

Runtime `69be4d260c111f67f7aa5e2780eb2d1c0b8c04d93f3862fe433bff7f59cb66b9`
is installed in physical slot B, retaining the preceding audio fixes and the
standard native-resolution settings. Package verification checks all 1,588
members; only the runtime and its matching digest change.

| Trial | Off before | On | Off after |
| --- | ---: | ---: | ---: |
| 1 | 11.665 FPS | 11.661 FPS | 11.687 FPS |
| 2 | 9.478 FPS | 9.409 FPS | 9.638 FPS |
| 3 | 9.656 FPS | 9.606 FPS | 9.639 FPS |

The camera changed between trials 1 and 2. Each individual off/on/off trial
passed its camera-consistency check; do not compare their absolute FPS as a
build-to-build result. Reuse was active with nonzero hits in every enabled arm.
Relative to each trial's two off arms, throughput changed by -0.128%, -1.559%
and -0.430%. There is no demonstrated performance improvement.

Steady accounting reports show index preparation at 1.75 / 1.82 / 1.76 ms in
trial 1 and 2.77 / 2.73 / 2.77 ms in trial 3. The latter's small index saving
does not produce a total preparation or FPS gain. Keep reuse off: validating
entries and capturing cache misses can offset the avoided copies. All trials
restore the configured policy and the captured logs contain no STOP; stationary
tests do not establish stability during driving or campaign combat.

The controlled selector remains available to test other workloads. Private
evidence, camera records and the accounting parser are in
`physical-index-reuse-benchmark/` under the engine-restructure validation directory.
The prior emulator comparison and charged-plasma smoke test completed, but a
monitored later exit was SIGKILL, with sender/cause unknown. The user confirmed
Claude is also using Vita3K and requested skipping further emulator validation
for this performance work. Continue with host correctness checks and physical
Vita tests; do not interpret those forced process exits as proven game crashes.
