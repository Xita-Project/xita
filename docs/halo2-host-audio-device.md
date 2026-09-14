# Halo 2: first real host audio device

Native90 executes the original successful DirectSound creation path using a real
Vita output port and the shared software mixer. The original capability,
distance, rolloff and speaker queries complete. The next sound method stops
explicitly at `37B637`. The first presented framebuffer remains entirely black;
there is no visible movie/menu or game audio.

This is a separate `--audio-host` profile. The original strict APU path and
[`--audio-unavailable` failure diagnostic](halo2-menu-audio-dependency.md) remain
available. Native87 reached map initialization through the failure path; native90
enters the earlier sound success path that native87 skipped. Native90 therefore
does **not** establish that a working sound device has reached map initialization.

The subsequent [mix-bin milestone](halo2-mix-bin-headroom.md) advances this
original success path to deferred Doppler setup; native90 remains preserved.

## Checked ABI and ownership

The adapter verifies the complete owned executable SHA-256, each of seven entry
ranges and both original reference-method vtable slots. Exact ranges/hashes are
in `AUDIO_HOST_BOUNDARIES` in `games/halo2_5849/hooks.py`; private disassembly is
in `../private/native87-audio-audit`. No original instruction bytes are tracked.

| Entry | Signature at replacement boundary | Return stack cleanup |
| --- | --- | --- |
| `37D797` | DirectSoundCreate(device GUID, interface output, outer unknown) | RET12 |
| `37B5AE` | GetCaps(public interface, four-DWORD output) | RET8 |
| `37B5CA` | GetSpeakerConfig(public interface, DWORD output) | RET8 |
| `37D506` | SetDistanceFactor(public interface, float bits, apply) | RET12 |
| `37D5CD` | SetRolloffFactor(public interface, float bits, apply) | RET12 |
| `37A14F` | Common-header AddRef(internal base) | RET4 |
| `37C70F` | Common-header Release(internal base) | RET4 |

The creation subset accepts null GUID/outer pointers. It validates the entire
argument/output mappings before mutation, including noncontiguous pages,
overflow and aliases of the owned device page. Successful creation requires an
actual output worker to submit its first mixed grain before allocating the
4 KiB guest device. The returned public interface is internal base+8, matching
the original wrappers' rebasing. Only the independently audited common vtable
and reference header are populated; there are no fabricated DSP or voice fields.
After host creation, all unimplemented original DSOUND function entries stop
before interpreting this opaque host device. Startup constructors can still run
before its first creation. The guard remains active after final release.

Repeated creation shares the device and increments its reference count without
opening a second worker. Last release joins the worker, closes its resources and
then frees the guest page. Host open/allocation failures leave caller output
untouched and return `88780078`/`8007000E` respectively after successful rollback.
A failed rollback or join is terminal and retains resources a worker may use.
The reference count represents external host-interface ownership; it does not
imitate the original driver's internal child-object reference accounting.
Successful calls preserve the full guest CPU state except EAX and return-stack
cleanup, and restore native FPSCR after host operations.
Device mutations rely on the existing cooperative guest scheduler: only one
guest fiber executes at a time, and these adapters do not yield to another guest
fiber. The concurrent output worker accesses mixer state under its own mutex;
it never reads or writes the adapter's device/reference registry.

GetCaps reports actual free mixer voices (initially 192), zero implemented 3D
voices, zero hardware scatter/gather entries and the actual 4096-byte guest
allocation. Buffer/stream factories are still unsupported, so this is mixer
capacity, not a claim that every DirectSound voice API works. Xbox stereo is
configuration value **0**, independently confirmed along with the four-DWORD
capability layout in the pinned [Cxbx Xbox sound definitions](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/XbDSoundTypes.h).
The [corresponding public method declarations](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/DirectSound/DirectSound.hpp)
corroborate query/setter signatures; the owned caller and wrapper determine the
exact XDK5849 ABI used here.

Listener values retain exact IEEE bits. The supported distance domain is
`FLT_MIN..FLT_MAX`, and rolloff is `0..10`, including negative zero. These bounds
follow Microsoft's [distance](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418022(v=vs.85))
and [rolloff](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/bb318698(v=vs.85))
documentation; out-of-domain inputs stop rather than fabricating a result.
Apply=1 stores deferred state; apply=0 commits pending listener values, matching
the observed original dirty-bit/commit path. There are no active 3D voices in
this subset, so it stores state without claiming spatial sound processing.

## Real worker and synthetic output evidence

`audio_vita.c` owns a SceAudioOut stereo port at 48 kHz, a mutex, a ready semaphore
and a 1024-frame double-buffered worker. The unchanged shared PCM/ADPCM mixer
algorithms feed that worker. Compile-time names select the Halo 2 sink, without
using the CE audio worker. The only shared-runtime addition is a locked,
read-only free-voice count. No existing CE mixer behavior changes.

The asset-free `audio-probe` utility uses synthetic PCM16 samples crossing
independently mapped guest pages. The final native probe reports:

```text
synthetic_pcm_probe=PASS
rate=48000 channels=2 format=PCM16 frequency=1000
before_grains=1 before_nonzero=0
playing_grains=14 playing_nonzero=12 peak=3600 error=00000000
stopped_grains=17 stopped_nonzero=13
reopen_close=1
```

These are real mixer/output submissions. The lab uses SDL's dummy audio driver,
so this does not establish physical audibility. The final probe returned to
Vita3K's idle state and its process was stopped. Earlier probes, and native90,
encountered a Vita3K SIGSEGV during utility relaunch **after** the application
closed and wrote its complete evidence; no claim is made that this emulator
shutdown defect is fixed.

The final probe is frozen in `../private/audio-probe/final2-artifacts`:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `84a13dae655a4ab2f2b41cd3d81478aeb35763081772c00912d0c2d8bad851ce` |
| EBOOT | `969c43c9ef42e3cb5ccf0f0f1c1f8872f745161736ee420aa4d1d8049bc84fd8` |
| VPK | `56f410ef8cffb27b347a5730b727eba89a6275e8ec0b0ddf5a2441ea2120c928` |
| Report | `56591b6fcc816acb2bd8ba768f7a22966e0ebfd3683fda6c1e07bb070992dfb2` |

## Native90 stop and next task

The original `21E516` call returns to `21E51B` with interface `00936008` and one
reference. GetCaps returns `192,0,0,4096`; distance is `4043126F`, rolloff is zero,
and speaker configuration is zero. At the next original call:

```text
entry=0037B637 return=0021E5E4 esp=005E5EFC
interface=00936008 mix_bin_index=0 headroom=0
grains=1 nonzero=0 peak=0 error=00000000
terminal worker/port close result=0
```

The owned caller loops over **indices 0 through 31**, not a mix-bin bitmask.
Wrapper `37B637` (32 bytes, SHA-256
`9cd8e91683d0bd423aefc6a5ec381a25191ffc3ea7cd0a7f3c3a91933b2e3dcf`)
rebases the interface, calls `37A3FE` and returns with RET12. That method stores
the headroom byte at listener+index+10h and invokes `37ECCA`. Private exact
caller/internal evidence is `../private/audio-host/native89-next-boundary.txt`.
The next bounded task is to audit that operation's actual gain/routing semantics
and implement its supported mixer behavior, then the following Doppler/deferred
work boundary. No mix-bin, buffer, stream or effect API is silently accepted.

Native90 GET=PUT=`03B43280`, packet remainder zero. Its stored first scanout has
522,240 pixels, all `FF000000`. The capture/video is black and contains no menu;
the application exits quickly at the explicit stop. The complete artifacts are
`../private/native-90-artifacts`, `native-90-view` and `native-milestone-90.json`.
Native89's earlier equivalent run remains separately preserved (GET/PUT
`03B41280`). Native88 was a failed pre-launch capture, not an emulator milestone.

| Native90 artifact | SHA-256 |
| --- | --- |
| ELF | `7becbd1a53a40dba87bda471b462636ae94622e0e040056b736e675fb07c68e2` |
| EBOOT | `7fafec1bf390f6eb2dd33a83121c1d1f5914e3bb00ed5ca57b6021abfb8d5e9e` |
| VPK | `60d67c397ecb13ffc3f7ba70ef4ae2fce5fa11ed04e6945736242911b71b6a07` |
| Trace | `3eb42fbcae188505d949fa64d38c7785b0046f98e6a07cd6d7bdae0e51112603` |
| Channel JSON | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Stored first frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

## Validation and reproduction

All 27 host test executables and the timed/active channel modes pass. New tests
cover the full guest ABI and output-write footprint, failure rollback, mapping
aliases, reference sharing/destruction, float boundaries/deferred state and the
concurrent output worker's open/write/join/release failures. The actual mixer
passes PCM page-crossing, exact output, stop and voice-capacity tests. All three
new C tests pass ASan/UBSan. The 38 focused Python profile/callback/ABI tests pass,
including every new fingerprint and mutually exclusive profile selection.

From the source directory, using a Python environment with iced_x86:

```sh
python -m unittest tools.test_halo2_audio_host tools.test_halo2_callback_roots \
  tools.test_halo2_gpu_bus tools.test_halo2_sparse_jump \
  tools.test_halo2_fp_environment tools.test_game_profiles
make -C games/halo2_5849 test-host BUILD=/path/to/private/host-tests
python games/halo2_5849/prepare_boot.py /path/to/owned/default.xbe \
  --host-channel --audio-host --out /path/to/private/audio-host
make -C games/halo2_5849 -j4 HOST_CHANNEL=1 AUDIO_HOST=1 QUAD_RENDER=1 \
  GUEST_OPT=-O0 GENERATED=/path/to/private/audio-host/generated \
  BUILD=/path/to/private/audio-host/build IMAGE=/path/to/private/audio-host/halo2_image.bin \
  QUAD_SHADERS=/path/to/private/prepared-quad-shaders
make -C games/halo2_5849 audio-probe BUILD=/path/to/private/audio-probe/build
```

Use a new build directory for the new profile. The quad shader preparation is
unchanged from the [quad probe instructions](halo2-quad-gxm-probe.md). Native90
uses private `audio-host/generated`, `audio-host/build` and
`quad-shaders/prepared`. Its automatic discovery report contains 11,764 candidate
functions, 161,187 basic blocks, 1,121,588 instructions and 3,832 unsupported
instructions. These counts are not runtime validation or compatibility.

The game diagnostic package embeds owned image/code and must not be uploaded as
a distributable release. Owned assets, generated source, shaders and packages
remain outside Git. The separate synthetic audio probe contains no game assets.

For an exact replay in the existing isolated lab, from `../private`:

```sh
python3 run_lab.py audio90-review native-90-artifacts/halo2-boot.vpk
```

No input or cache reset is needed to reach this early sound stop. The helper uses
only the existing Halo 2 title `XH2B00001` and display `:111`. After collecting the
stop, use `python3 run_lab.py stop`. The synthetic utility uses its own title
`XH2A00001` and writes `ux0:data/xita-halo2/audio-probe.txt`. No Halo CE emulator,
installed Vita, USB device or game asset was modified during this milestone.
