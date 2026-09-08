# Display callback stack recovery — September 5, 2026

A campaign mesh capture exposed a gradual guest-memory overwrite. At emulator
frame 18,320, all 672 referenced vertices of a cryo pod contained the repeated
word `0x03D0D480`, including positions and bone indices. Adjacent pod parts had
the same contents. That word was the display-timing callback's argument pointer.

The host callback bridge pushed an argument and a return address, then assumed
the guest removed both. Halo's actual vblank function, `0xBB4E0`, ends in plain
`RET` and removes only the return address. Every vblank therefore leaked four
bytes from the dedicated callback thread's guest stack. At 60 Hz, that is 240
bytes/second; its 64 KiB stack is exhausted in about 4.5 minutes. The next writes
cross virtual pages outside the allocation, eventually reaching map model data
through their default physical mappings. This explains the pointer pattern and
why those nominally static vertex buffers changed while a frame was in flight.

The callback bridge now restores its entry stack pointer after the guest returns.
It owns this temporary call frame, so this also supports callbacks using `RET 4`
and nested callbacks without double cleanup. Return-register values and guest
side effects remain available to the caller. No new persistent memory or copy
pool is needed.

An experimental snapshot of runtime-created vertex buffers was removed: the
affected buffers are map-registered, and the captured data was already corrupted
before draw recording. That experiment did not fix the observed distortion.
The existing per-frame index retention remains in place.

Validation:

- `make -C recomp/host test-callbacks` executes the locally recompiled Halo
  vblank function 216,000 times (one simulated hour), checking the counter,
  event branch, callback data, stack position, and surrounding bytes. It also
  exercises `RET 4` and nested callbacks. The previous bridge fails the stack
  assertion on its first call; the corrected bridge passes.
- AddressSanitizer and UndefinedBehaviorSanitizer pass. The sanitizer build
  disables global instrumentation (`--param asan-globals=0`) so unrelated
  DirectSound dispatch tables can be discarded by the host linker; the guest
  arena is heap-allocated and its explicit stack guards remain checked.
- The native Vita build passes. A fresh emulator campaign run completes the
  bridge/hangar/cryo opening and returns to first person. Cryo-pod models remain
  intact after more than 11 minutes, including camera turns; sampled draws show
  no vertex/index mutations before GPU completion. Flashlight-on still hides
  the pod body immediately, and switching it off restores it: a separate bug.

Hardware stability, flashlight/plasma lighting, and angle-dependent visibility
still need their own checks. This establishes a concrete corruption mechanism,
not that every previous geometry report had the same cause.

Evidence is archived in
`/home/birchwoodgod/xita-backups/2026-09-05-224334-lighting-investigation/`, including
the unmodified bridge, corrupted mesh captures, and emulator log.
