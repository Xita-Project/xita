# Weapon and lobby corrections — September 8

These changes were [installed and verified over USB](hardware-20260908-weapon-menu.md)
on September 8 at 07:00 CDT. Hardware gameplay and listening checks are pending.

## Weapon clipping at walls

A fresh Blood Gulch capture reproduced the wall cutting away the front of the
assault rifle. The guest viewport ranges remained 0–1; changing viewport depth
alone would not address this capture.

Halo draws the first-person model with stencil reference 1, an always-pass test
and a masked replace operation. Later world draws test for stencil value 0.
The bridge previously ignored both the dedicated stencil setters and the
corresponding NV2A methods, so a nearby wall could overwrite the weapon.

The bridge now captures all eight stencil fields with each draw, translates the
comparison and operations to GXM, and restores them at scene/UI boundaries.
Stencil clears retain their value, survive coalescing with color/depth clears,
and reset the mask each frame. UI and the performance overlay neither test nor
write the scene stencil mask. Repeated mesh state is cached. No depth override,
draw reordering, new shader, GPU allocation, or full-GPU wait is added.

The initial emulator correction keeps the complete rifle visible against a
close base wall. The trace confirms the weapon's replace operation and the
world's equal-zero test. Native hardware, other weapons and later missions
still need confirmation.

Register identities were checked against the
[xemu NV2A definitions](https://github.com/xemu-project/xemu/blob/master/hw/xbox/nv2a/nv2a_regs.h);
GXM uses the installed VitaSDK declarations. The implementation is local code,
not a copied emulator implementation.

## Split Screen menu

The earlier solo-start patch lowered the host's minimum player count, while
two display checks still required two players. This produced a misleading
waiting message and overlapping text during the countdown. Both display checks
now use the same solo/System Link minimum as the host initializer.

The split-screen wrapper and lobby widget also set the game-pause flag. Their
constructor now suppresses that flag for those two identified widgets. Both the
tag ID and widget name must match. The earlier last-opened-map guard failed:
Halo starts reading Blood Gulch while still displaying the lobby, so that flag
already says the UI map is gone. Gameplay pause and other widgets retain their
original flag. Existing pause-count acquisition/release remains in charge.
The patch checks every generated signature before writing and is idempotent.

## Crackly menu music

The menu uses stereo 44.1 kHz Xbox ADPCM. Its queue stayed supplied in the private
emulator trace (typically three or four packets); that does not rule out hardware
starvation. Inspection and synthetic tests found two separate faults:

- Audio reads assumed adjacent guest pages were contiguous in host RAM. A block
  crossing separately backed pages decoded unrelated bytes. Stream assembly now
  uses the runtime's page-aware reader. Buffer playback keeps direct reads within
  a page and gathers only the chunk that straddles a boundary.
- The sink loop immediately remixed the same unaligned output buffer. It now
  alternates two 64-byte-aligned grains, preserving the previous submission
  while mixing the next. This adds 4 KiB of static output storage; grain size,
  rate and scheduling stay the same.

The [VitaSDK output contract](https://github.com/vitasdk/vita-headers/blob/master/include/psp2/audioout.h)
and [SDL's Vita audio backend](https://github.com/libsdl-org/SDL/blob/SDL2/src/audio/vita/SDL_vitaaudio.c)
were consulted for buffer handling. No SDL implementation was copied or linked.
ADPCM decoding and resampling are unchanged. These are reproducible correctness
fixes, but audible improvement on the Vita is still unconfirmed.

## Validation

- Native Vita builds pass in the isolated candidate stage.
- `make -C recomp/host test-stencil`: emitted GXM state protects a marked weapon
  pixel from a closer wall while allowing unmarked world pixels; verifies
  clears, UI isolation, snapshots, operation mapping and state-cache invalidation.
- `python3 tools/test_ps_methods.py`: actual dedicated/generic state handlers,
  argument cleanup, reference truncation and masks pass.
- Draw-state checks cover stencil-only and merged clears without losing depth
  or color values. Existing RTT and frame-retirement checks pass.
- `make -C recomp/host test-split-screen`: real generated initializer, status,
  start and countdown paths pass for zero, one and two players, blocked child
  menus and unchanged System Link. Lobby pause/register checks pass too, also
  under ASan/UBSan. The widget guard is tested with both values of the unreliable
  last-opened-map flag, mismatched names and unrelated widgets.
- `make -C recomp/host test-audio`: PCM8/16 and mono/stereo ADPCM agree with
  contiguous-memory controls across every possible block/page split, including
  packet boundaries. A retaining sink checks immutable, aligned output over eight
  submissions. The old code fails; the corrected code passes, also under ASan/UBSan.

The combined stencil/status build passed normal solo countdown and Blood Gulch
launch, close-wall rifle firing and flashlight use, grenade death and respawn,
return to the main menu, and the opening campaign cutscene skip into cryo gameplay.
There were no logged upload failures, draw drops, fence errors or full-GPU waits.
The final executable also passed lobby animation (captures three seconds apart),
normal countdown and Blood Gulch launch, firing, Pause / Leave Game, and return
to the animated main menu. The emulator's audio output was muted, so this is
not an audible music-quality test.
Hardware performance gains are not established by these checks.

## Next hardware check

On the installed candidate, leave the main-menu music playing for a minute,
then enter a solo Blood Gulch match through Split Screen. Walk directly against
a base wall with the rifle, fire, turn the flashlight on and off, and try Pause /
Leave Game. Repeat the usual benchmark with unchanged graphics settings. Report
music crackle, weapon visibility and any GPU crash separately from the FPS result.
