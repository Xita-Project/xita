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

Physical performance of index reuse remains unmeasured. The candidate is also
being used to measure periodic profiling cost, with index reuse left at its
default off on the physical Vita.
