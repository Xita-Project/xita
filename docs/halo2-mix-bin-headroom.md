# Halo 2 indexed mix-bin headroom

The opt-in host audio adapter now implements the original `37B637` per-bin
headroom call. Its gain changes affect real mixed samples before output clipping.
The native synthetic PCM probe confirms independent left/right attenuation and
restoration. This does not implement game buffers, streams, effects or surround
routing, and does not establish a visible Halo 2 menu or game audio.

## Original semantics

The owned XDK5849 public wrapper takes `(public interface, bin index, headroom)`
and returns with RET12. It rebases the interface by eight bytes and calls
`37A3FE`. That method stores the low byte of headroom in listener+10h+index and
calls `37ECCA`. The latter masks that byte with 7 and writes
`FE820200 + 4*index`. The original game caller `21E5D0` loops over indices 0..31.
The original listener constructor `37CC6A` initializes bins 0..30 to 1 and bin31
to 0. These defaults and truncations are retained exactly. Out-of-range indices
stop before backend/device mutation; headroom is a DWORD whose low byte is
stored and whose low three bits select gain.

The pinned [xemu method handler and voice mixer](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/vp/vp.c)
independently identify the indexed submix headroom write and apply gain
`1 / (2^headroom)` to contributions to that bin. The [register definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/mcpx/apu/apu_regs.h)
confirm the 0x200 base, three-bit mask and 32 bins. The pinned
[Xbox sound definitions](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/XbDSoundTypes.h)
identify bins 0/1 as front-left/front-right. These sources establish the modeled
operation, not the behavior of an entire DSP program.

Exact private disassembly/hashes are in
`../private/audio-host/headroom-original-functions.txt` and
`headroom-original-check.json`. A private Unicorn check ran 8,320 cases through
the original public/internal/PIO functions: all byte values plus four wider
DWORDs for every bin. It verifies exactly one changed listener byte, one selected
PIO write, EAX=0 and the return-stack cleanup. Critical-section/IRQL helpers are
isolated and FIFO readiness is supplied in this test; it is not a working APU.

## Mixer integration and limits

`audio_bins.c` stores all 32 headroom bytes under the existing mixer mutex.
Only the mixer's current fixed mono/stereo front-left/front-right route consumes
them. Changing an unused surround/effect bin stores its configuration without
rerouting audio or asserting that an effect processor exists. Unsupported
buffer, stream and routing APIs remain explicit stops.

The shared mixer has a compile-time `XK_AUDIO_OUTPUT_FILTER` hook immediately
before its output saturation and while its lock is held. Only the H2 audio build
selects `h2_audio_bins_filter`. Because all current contributions to each stereo
channel use the same bin gain, applying that linear gain to the unsaturated sum
preserves the supported route. Applying it after int16 clipping would lose loud
sample information and is explicitly tested against. This retains the existing
software mixer's integer floor quantization and independent user master volume;
it is not an emulation of the APU DSP's full precision or voice/effect pipeline.

The default ARM `xk_audio_mix` section is identical to commit `6366b74`: 1,988
bytes, SHA-256 `4e4ed1de629ee9ccd422120ef7670fa95eb1d6c642d0ece80aee23532808cf88`.
The default PCM test also retains its exact outputs. No CE sink or behavior is
changed. Device creation resets bin defaults before starting the output worker;
recreation and final release retain the previously tested lifetime contract.

## Tests and native output

All 28 host executables plus timed/active channel modes and 38 focused Python
tests pass. New synthetic coverage includes 64 combinations of left/right gain,
12 simultaneous voices exceeding both int16 limits, page-crossing PCM, signed
accumulator extrema, zero/no-op gain, byte truncation, all 32 stored bins,
invalid-index rejection, unused-bin isolation, reset and full guest ABI/write
footprint. The bin, device and concurrent worker tests pass ASan/UBSan.

The native asset-free utility, through the actual mixer and SceAudioOut worker,
reports:

```text
synthetic_pcm_probe=PASS
rate=48000 channels=2 format=PCM16 frequency=1000
before_grains=1 before_nonzero=0
playing_grains=14 playing_nonzero=12 peak=3600 error=00000000
headroom_1_2_peaks=1800,900 restored_0_0_peaks=3600,3600
stopped_grains=25 stopped_nonzero=21
reopen_close=1
```

The SDL dummy driver is used in this isolated lab, so this is accepted nonzero
output and gain evidence, not physical audibility. The utility returned to idle
and was stopped. Its frozen artifacts are `../private/audio-headroom-probe/artifacts`:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `bc22046850ada156339594324ec84bd2f7b9f193baccdf9450347c0ebc7b3b9d` |
| EBOOT | `cb7f6f56957d56f28c0a06464394bb3d8dc1ff6ee2751b78b1f573d6f7292b07` |
| VPK | `eb93a53646169c35dfcfb02d0d73cee9472e6d5aa95364d657bd85d8177cd124` |
| Report | `fb959dfdaa405b4e3b136f23635cdb24c19a496735e1a7692265485f9a14ea51` |

## Native91 game result and replay

The original game executes every bin index 0..31, setting each stored byte and
effective shift to zero, with return `21E5E4`. The next original call stops at
`37D52A`, return `21E5FF`, ESP=`005E5EFC`, with interface=`00936008`, Doppler
value bits=0 and apply=1 (deferred). The real output worker remains healthy,
has submitted one silent grain, and joins/closes successfully at this explicit
stop. No game buffer or stream has yet been created through the host adapter.

GET=PUT=`03B43280`, packet remainder zero. Frame1 again has all 522,240 pixels
`FF000000`; the capture/video contains no visible menu. This run is still in the
earlier sound success path, before native87's later no-driver map path. The
application returns to Vita3K idle and the lab process is stopped afterward.
Private frozen evidence is `../private/native-91-artifacts`, `native-91-view`
and `native-milestone-91.json`.

| Native91 artifact | SHA-256 |
| --- | --- |
| ELF | `fa9fa585d631c0b2496c7d9ffa5167ec840045b30fdd0c1360e00fa590c0ac3a` |
| EBOOT | `4ffce0d733a717793a32b81b41c738503f0808f6d710a16a43afd8dc028cbd44` |
| VPK | `222ec16dfd8876a319dbfac36b37588183d6deb60c30d561d5d9ab2783355596` |
| Trace | `d4bf5ff95316978acd5724f6ae2ce7e8fb02026d8f5d56dbd7d0762ae99a237e` |
| Channel JSON | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Stored first frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Use the [host-audio build instructions](halo2-host-audio-device.md), with a new
private `audio-headroom` generation/build directory and the existing prepared
quad shaders. The game build uses `HOST_CHANNEL=1 AUDIO_HOST=1 QUAD_RENDER=1`
and `GUEST_OPT=-O0`, with four build jobs. There are no discovery changes:
11,764 candidate functions and 3,832 unsupported instruction occurrences remain
automatic counts, not execution coverage. The synthetic utility uses
`make -C games/halo2_5849 audio-probe BUILD=/path/to/private/audio-headroom-probe/build`.

For the exact archived game build, from the existing `../private` lab directory:

```sh
python3 run_lab.py headroom91-review native-91-artifacts/halo2-boot.vpk
```

No controller input or cache reset is needed for this early sound stop. Stop the
isolated instance afterward with `python3 run_lab.py stop`. Owned code, images,
generated source, traces and game-embedded packages remain private/outside Git;
the diagnostic game package must not be uploaded as a distributable release.
No CE emulator, physical Vita or USB device was touched.

The next bounded task is the independently identified `37D52A` Doppler setter
and its deferred-state contract, followed by auditing the original `379F5B`
DirectSound work call before allowing it to return. Buffer/stream/effect and
unsupported routing methods remain explicit stops. This milestone does not
supply a fabricated completion or substitute menu screen.
