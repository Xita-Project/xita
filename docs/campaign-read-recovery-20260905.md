# Campaign scene-load recovery — September 5, 2026

The a10 opening cinematic repeatedly froze while approaching the bridge window
in Vita3K, before Keyes appeared. The guest entered portal intersection routine
`0x51E90` with a malformed polygon, overflowed its stack, and looped indefinitely.
This was reproduced from a fresh launch without pressing any skip buttons.

The failure started with an incomplete read, rather than malformed source map
data. When switching from `a10_space` to `a10b`, the runtime requested 8,255,488
bytes from `cache000.map` at offset 6,232,064 into guest address `0x811C6800`.
Only 6,727,680 bytes arrived, but Halo treated the request as completed. The
unread region began at `0x81831000` and retained the exterior scene. The bridge
root pointed at its expected portal table, `0x8199BB7C`, whose bytes instead
matched `a10_space` exactly. Later invalid portal indices led to the runaway
polygon loop. All nine source BSP portal/cluster tables validated, and all nine
cached BSP regions matched the inflated source byte for byte.

The Vita file adapter now retries an incomplete positional read through a
temporary 32 KiB buffer and copies the recovered bytes into the destination.
Ordinary complete reads retain the direct path and allocate nothing. Retries
stop on EOF or error, preserve partial progress, and leave unread bytes intact.
The existing logical file-size handling remains separate. Memory use increases
by at most 32 KiB during a retry, with no permanent buffer or guest stack growth.

The protected-page explanation is supported by the failure boundary, Vita3K's
direct destination read path, and a host reproduction. Vita3K's
[sceIoPread implementation](https://github.com/Vita3K/Vita3K/blob/master/vita3k/modules/SceLibKernel/SceLibKernel.cpp)
passes the destination to its
[file reader](https://github.com/Vita3K/Vita3K/blob/master/vita3k/io/src/io.cpp).
On Linux, a positional read into a buffer containing a protected page can stop
at that page without invoking the user-space fault handler. Reading into
temporary storage and copying with CPU instructions lets the memory tracking
handler process the write. The host regression reproduces that distinction with
a real protected page, positional file reads, and a write-fault handler.

Validation:

- `make -C recomp/host test-read-retry` passes direct reads, repeated partial
  reads, initial zero progress, EOF, errors, destination guards, and the real
  protected-page case. AddressSanitizer and UndefinedBehaviorSanitizer also pass.
- The native Vita build passes. The emulator logs recovery of the missing
  1,527,808 bytes in 47 staged reads and receives the full requested scene.
- The portal's first word becomes the correct `0x000E000F`; the cinematic
  proceeds into the bridge with Keyes, through the hangar, and into the cryo bay.
  The final 6,230,016-byte cryo scene load also recovers through staged reads.
  Without skip input, camera control returns to the first-person callback
  `0x11E750` with `camera_control=0`; look input and exiting the pod work.
- Temporary diagnostics in the generated portal/load/switch routines are
  removed from the source. The opt-in `XV_SPIN_BT` runtime diagnostic can now
  identify an ARM caller relative to `xv_preempt`, even in an untraced build.

Hardware validation, later campaign transitions, and cutscene skip/camera
handoff are separate checks. This does not establish that every reported
geometry spike or missing asset had the same cause.

Evidence, exact diagnostic ELFs, map comparisons, and screenshots are archived
under `/home/birchwoodgod/xita-backups/2026-09-05-224334-lighting-investigation/`.
