# Halo 2 recurring original frame flips

Native78 retires recurring original interval-one flips, executes the original
vblank callback, and presents the resulting guest framebuffer. The game opens
`d:\maps\mainmenu.map` and issues three 2048-byte header reads. It has not shown
the menu. The live images and last presented frame remain entirely black.
Repeated draw/presentation completion is not evidence of correct later Bink
video decoding.

## Audited boundary

The native77 queue request is method `0100`, value `74280021`. Running original x86
`3FF240`, `3FECC0` and `3FF548` in a private Unicorn probe over captured device
state independently establishes slot 1 pending=1, due=4, address 03A14000,
next deadline 5, submitted count 2. At vblank 3 the original consumer retires
nothing and makes no MMIO access. At vblank 4 it retires exactly one entry,
selects PCRTC_START=03A14000, increments the 3D read index and sets retired count 2.
The controlled RDTSC in that probe affects only the unused swap-timing record;
it does not establish native timing or presentation.

The runtime executes those original helpers. It accepts only the observed
progressive 640x480 / pitch 2560 mode, interval 1, modulo 2, one pending entry, known
callback/event object and no gamma update or secondary swap callback. It checks
both queue slots, counter/parity relationships, complete framebuffer and stack
mappings, tile metadata, and the actual callback inputs before queue mutation.
Counter rollover and other display modes remain unsupported.

A blocked flip-stall command waits for a real Vita vblank. A failed wait or an
unchanged host vcount preserves the pending guest queue and CPU/FP state. After
a successful wait, the original consumer may issue precisely one PCRTC_START
write from 3FF5BC and the PGRAPH_INCREMENT read/write pair from 3FED43/3FED4C.
The adapter implements READ_3D as `(read+1)%modulo`, consistent with pinned
[xemu PGRAPH_INCREMENT semantics](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c).
These accesses outside the original retirement context remain fatal.

Retirement precedes the actual KeSetEvent and original 12B2A0 callback with record
`{vblank count, retired swap count, 1}`. The complete interrupted x86 context
and native FPSCR are preserved. The existing real presenter then displays the
original framebuffer and the parser retries its stalled word. This synchronous
adapter may consume two host vblanks per guest flip because presentation also
waits; no cycle-accurate pacing or general interrupt implementation is claimed.
No arbitrary MMIO or unsupported draw becomes successful.

## Native78 evidence

The first recurring swap queues 03A14000 for guest vblank 4 and presents it after
real vcount 143→144, with presentation vcount 145. Subsequent swaps alternate
038E8000 and 03A14000. The run completes 381 retired swaps and 382 presentations
including the initial mode presentation; no unsupported draw was discarded.
The guest performs original movie conversion and quad submission throughout.
After a configured Enter/Start key press in the isolated :111 window, it reaches
an uninstrumented original inline queue-status read in 12D0A0 at 12D0D5:
FD003240. The checked address guard stops there. Existing channel semantics can
provide real PUT/GET and busy state once these exact audited reads are routed
through the bus adapter; they must not be replaced by constant idle results.

Terminal GET=PUT=03BE37B8, all pending packet words consumed. The final displayed
capture has header `{960,544,3840,382}` and 522240 pixels of FF000000. Actual desktop
captures are black. The three mainmenu header reads are a new I/O milestone;
full map loading, a visible intro, menu rendering and gameplay remain unverified.
The explicit unavailable-audio diagnostic still creates no audio device.

| Private native78 artifact | SHA-256 |
| --- | --- |
| ELF, 75,186,832 bytes | `bf21cf8d2a74af64d2fc1ad7c51a479ba9e736e9019071add54bdd55719f2e89` |
| EBOOT, 91,899,454 bytes | `4ad8e786e1f2ace3459918cffdc5c8445f89d87f17c19d923e0fc1007f332bf6` |
| VPK, 25,618,727 bytes | `b8b486329019213fb4424661c3aa55a03ad07aa4d2cfd94baa116db80904da2b` |
| Boot trace | `267561a66ddcd9201dec110578beb7e0cbc1859f7390d1f7af4ddd83a06f8bec` |
| Channel JSON | `2e5b644de97afd288deeeb96d61a8370a792ac1bfad796248428c44102b59ab6` |
| Last presented frame | `16d993d1df3d253bbdaf3fabae632e3649fc902055bd4660f5badf351f905e9f` |

All 23 host executables plus timed and recurring runtime cases pass. The recurring
case also passes ASan/UBSan and covers both slot parities, failed/unchanged waits,
complete mappings, callback-object validation, event/callback/presentation order,
full CPU/FPSCR preservation and rejection of retirement MMIO outside its context.
Host helper mocks test the bridge; native78 separately executes original code.

Replay from the isolated source directory:

```sh
python3 ../private/run_lab.py replay78 ../private/native-78-artifacts/halo2-boot.vpk
```

The lab maps Enter to Start; press/release it in the own :111 game window after
movie draws begin to reach the post-movie queue-status boundary. Exact timing
changes the completed draw count, not the meaning of this unsupported access.
Build selection and private shader preparation remain as documented in
[the original quad milestone](halo2-original-movie-quads.md). The VPK embeds
owned game image/code and must remain private, never a distributable release.
All assets, generated code/shaders, buffers and captures stay outside Git.
