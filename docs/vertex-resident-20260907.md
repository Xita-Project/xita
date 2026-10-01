# Reuse exact retired-slot vertex bytes — September 7, 2026

The previous hardware run spent 5.922 ms/frame preparing vertex streams in its
last complete driving window. Frame-owned snapshots remain necessary: passing
mutable guest streams to an asynchronous GPU risks mixing geometry generations.
This candidate avoids re-uploading identical bytes already present in an acquired
slot. The subsequent [hardware result](hardware-20260907-vertex-resident.md) is
negative: 7.404 / 7.306 / 7.569 FPS off/on/off, with stream preparation increasing
from about 7.1 to 9.9 ms/frame. The next candidate disables reuse by default.
Driving stability remains unverified.

## Change and ownership

`xv_vertex_upload.c` retains an initialized byte prefix in each slot's existing
cached mirror and uncached GPU allocation. Reset still discards the current-frame
address table and resets allocation to offset zero. It runs only after the caller
has acquired the slot following its GPU-completion notification.

For each new snapshot, compare the requested bytes against the mirror at its
new destination offset. A full match proves the GPU already contains the exact
bytes this draw needs, regardless of source address, draw order, or map changes.
A mismatch copies a new snapshot as before. Same-frame mutations still append
separate versions; captured pointers are never overwritten. Repeated passes
retain their existing exact comparison and prefix-reuse behavior.

New alignment padding is initialized identically in both copies before it joins
the valid prefix. This matters when later frames change stream lengths and consume
bytes that previously belonged to a gap. Never compare against uninitialized data.
The GPU allocations remain mapped read-only for GXM. No new GPU wait, worker,
memory allocation, or vertex/matrix transformation is introduced.

`XV_VERTEX_RESIDENT=0` disables the new check; default was enabled in this original
candidate and is disabled after the hardware result. The serialized
recording-thread override supports controlled comparison and restores the chosen
configuration afterward. Existing 8 MiB GPU + 8 MiB mirror capacity per slot is
unchanged. Allocation failures and exhaustion retain the original failure path.

## Validation

- Host tests preserve pending draws across all three slots through 2,000 mixed
  slot generations, source mutations, varied lengths, and off/on switching.
  Boundary tests cover newly consumed padding, exact allocation limits, prefix
  reuse, different source addresses, and allocation/map failures. ASan/UBSan pass.
- Production frame-acquisition tests confirm benchmark phase switching changes
  the upload override while leaving queue policy fixed. Frame completion, pacing,
  benchmark restoration/cancellation and Vita input checks pass.
- Native build and compressed/padded SELF decoding pass against the archived
  native executable. Only six reviewed files were copied into the preserved
  generated-code staging baseline; unrelated root shader/visibility experiments
  remain outside this candidate.
- Private Vita3K renders the dashboard, menu, Blood Gulch, moving/firing and
  flashlight input, then the campaign opening and first-person cryo bay after
  skipping. Nine geometry captures report zero changed draws before completion.
  No normal Finish calls, fence errors, upload failures or storage drops appear.
  Maximum slot use is 1,577/8,192 KiB; maximum pending submission count remains one.
  The private instance was stopped and its prior executable/config restored.

The stationary Blood Gulch comparison measures 19.906 / 19.915 / 19.919 FPS at the
emulator's 20 FPS cap, with matching views and successful restoration. This does
not establish a Vita speedup. In each measured 60-frame window:

| Upload work | Off | On | Off again |
| --- | ---: | ---: | ---: |
| GPU copies | 4,620 | 0 | 4,620 |
| Bytes copied | 16,165 KiB | 0 KiB | 16,165 KiB |
| All compared bytes | 22,187 KiB | 38,352 KiB | 22,187 KiB |

The extra comparison replaces about 269 KiB/frame of uploads in this fixed view.
Selected static wall and sky rectangles are pixel-identical off/on/off. The ground
rectangle matches off/on, but part changes in the final off capture; no whole-frame
pixel-equivalence claim is made. Camera movement and animated campaign frames still
copy changed data. Campaign windows also show substantial same-frame comparison
traffic, so the copies alone are not the entire stream-preparation cost.

Emulator stream preparation rises slightly (roughly 0.36 to 0.44 ms/frame). Its
memory behavior differs from uncached Vita uploads: the native ELF imports
`sceClibMemcpy` but implements `memcmp` with scalar loops. We must measure the
comparison/write tradeoff on hardware before claiming this optimization helps.
The logs separate resident comparisons and skipped uploads from actual copies.

## Candidate and hardware test

Archive: `/home/birchwoodgod/xita-backups/2026-09-07-181523-vertex-resident/`.
USB candidate SHA-256:
`b997294e518673eaba068d1a903bb48cd0c3d1b8ca4e6a6f86be33c09128fb1f`.
The executable was written into the existing 30,891,526-byte allocation, then
direct-read verified and verified again on a fresh read-only mount. Settings,
saves and 1,654 other checked files remained unchanged. USB is safely unmounted.
The full record is in `deployment.json`. No VPK installation is needed.

Keep standard settings. In a stationary Blood Gulch first-person view, press
**L + R + Square**, release, and leave the camera still until the overlay clears.
This executable compares **vertex upload reuse off/on/off**, not triple buffering.
Each phase settles for 60 frames and measures 120. Single-flight submission,
shaders, effects, resolution, clocks and FPS cap stay fixed. Allow about 90 seconds
at the previous hardware speed. A second press cancels and restores the default.

Then drive briefly to check the still-unresolved GPU crash. The next collection
should compare actual throughput, stream-preparation time, comparisons, copied
bytes, GPU completion latency and any new crash dump. The earlier single-flight
recovery had not received a new gameplay test before this candidate was prepared;
do not describe its hardware stability as established.

Next actions depend on that evidence. Retain reuse only if its hardware tradeoff
is worthwhile. Investigate faster exact comparisons and the amount of vertex
data covered by index bounds; the current bounds retain zero through the maximum
index. Keep any index rebasing equivalent for every stream and primitive. Material
shader cost and the guest's flare-result dependency remain separate priorities.
The physical-Vita 20 FPS goal remains open.
