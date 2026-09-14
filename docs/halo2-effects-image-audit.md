# Halo 2 DSP image and explicit unsupported-effects diagnostic

Native92 stops at the original `XAudioDownloadEffectsImage` boundary. The
image's metadata can be recovered from the owned executable, but a successful
download requires code decryption, DSP memory/descriptor relocation and DSP
acknowledgement. This increment supplies a checked metadata reader and a
separately enabled, genuine failure result. It does not supply an effects
processor, descriptor substitute or working menu.

## Owned image and consumers

The XBE SHA-256 remains
`03215919bb7163259257d361f4c7bf802a7ab12aa85e2689436369b5c427935d`.
Its preloaded `DSPImage` section at `0057DD00` is 28,768 bytes, SHA-256
`e54e4d29d02a3cdaba4be942d702b5e5b29f945d18215d99187603988fa34745`.
The six-word header at section offset `800` describes 3,552 code words and
2,970 state words. The code begins at `818`, state begins at `3F98`, and the
15-effect descriptor begins at `6E00`. The descriptor occupies 488 bytes,
followed by exactly 120 bytes of per-effect key material. It requests 774,144
bytes of scratch storage. Code bytes are encrypted; the initial state bytes
and descriptor metadata are readable. Keys and code are not emitted by the
metadata tool.

Each effect map has eight DWORD fields: code offset/size, state offset/size,
Y offset/size and scratch offset/size. The observed map sizes and offsets are
**bytes**, even though the enclosing header's code/state lengths are words.
For example, the first 144-byte code map ends exactly at the next code map.
The original decryption call consumes each map's size directly as a byte count.
The four immediately requested state records are:

| Effect | State image offset | State bytes | First requested byte range |
| --- | --- | --- | --- |
| 4 | `41CC` | `120` | `41EC..41F3` |
| 5 | `42EC` | `120` | `430C..4313` |
| 6 | `440C` | `120` | `442C..4433` |
| 7 | `452C` | `120` | `454C..4553` |

Values in this table are hexadecimal. The original caller `191300` supplies
location indices 9 and 10, then requests eight bytes at state offset `20` from
each of effects 4..7 using public `GetEffectData` at `37B5E6`. Those image
bytes initially contain DWORDs 0 and 30. The real getter reads relocated DSP
state; these file values alone do not justify returning runtime effect data.
No effect algorithm names or supported processing are inferred from indices.

The checked reader `tools/halo2_dsp_image.py` verifies header relationships,
code/state ranges, descriptor count/extent, alignment, scratch/Y arithmetic
and the exact trailing key-table extent. It outputs only metadata and hashes,
without decrypting, relocating, uploading or executing any payload. Use a
private destination and the existing private Python environment:

```sh
python tools/halo2_dsp_image.py /path/to/owned/default.xbe \
  --out /path/to/private/effects-image-metadata.json
```

## Original loader and failure contract

The complete public loader at `37B86D` is 514 bytes, SHA-256
`152b5769f3630d38073a25543bbce4d35f387ad3af712ad1b96f5427794f2b67`.
It takes four arguments and `RET 16`: image name, location pointer, flags and
output-descriptor pointer. Flags=1 selects the section path. On a successful
lookup/load it calls the internal device download at `37A289` and balances its
own section reference before returning the resulting HRESULT.

`37E496` copies the code/state/descriptor payload to GP scratch. `37E229`
allocates the descriptor, reserves scratch storage, decrypts the per-effect
keys and code, relocates code/state/Y/scratch fields, and copies the relocated
descriptor. It then writes command=3 at scratch offset `810` and spins until
the DSP clears it. For this image, the state relocation constant is
`FE82C268`; effect4's state pointer becomes `FE830434`. The getter `37E5D8`
copies from that relocated address, not from the original section.

The pinned [Cxbx loader implementation](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/DirectSound/DirectSound.cpp)
marks its DSP support incomplete and explicitly identifies encrypted-image
copying as incorrect. That success-return path is not reused here. An actual
DSP or a verified implementation of the required effects is still necessary
for a truthful successful download.

With `AUDIO_EFFECTS_UNAVAILABLE=1`, the audio adapter returns
`DSERR_UNSUPPORTED=80004001`, the documented result for an unavailable
operation. Its [meaning](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416775(v=vs.85))
and [numeric value](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416776(v=vs.85))
are specified by Microsoft. The diagnostic checks the exact caller `1913A9`,
section name including its terminator, flags=1, location indices 9/10, mapped
stack/input/output, live device and worker health. It leaves all guest memory,
output descriptor, device state, worker resources and section references
untouched. Only return EAX and stack cleanup change. The game retains its own
outer section load/unload and failure control flow. Unsupported formats,
callers, locations and flags remain terminal stops.

The default value is zero, retaining an immediate strict stop at this API.
The option requires `AUDIO_HOST=1`; it is a diagnostic, not a supported
no-effects mode. The existing native92 archive is preserved.

The original caller tests a negative HRESULT, skips all four GetEffectData
calls, unloads its outer section reference and returns false. The higher sound
initializer then calls its original cleanup `21EAE0`. Its public Release
wrapper `379F2A` is 22 bytes, SHA-256
`0a72b625b8e9e1a301ddf3c84ec304eded77fc8886c86f41f03efd6fa1fac2b5`.
This exact original wrapper now runs only from verified returns `21EB91`
(game cleanup) and `3E3D19` (Bink setup), with
checked stack, live public interface and compatible common-header vtable.
Its call reaches the previously tested Release adapter, which joins/closes the
real worker before freeing the guest allocation. Unknown original DSOUND
methods, buffers, streams and processing remain blocked.

## Checks

All 29 host executables plus timed/active channel modes and 41 Python tests
pass. Both the default strict device and explicit effects-failure build run
the actual device/lifetime tests. The failure build passes ASan/UBSan. New
checks cover the exact failure ABI, full CPU/FPSCR and memory preservation,
no resource mutation, cross-page name input, malformed locations/flags,
output aliases, missing mappings, release caller/vtable rejection, descriptor
truncation/alignment/count/range/overflow and metadata nonmutation.

A private original-XBE execution check runs the complete `191300` caller with
three failure HRESULTs. It observes unchanged descriptor output, no getter
calls, the outer unload call and false return. Section/arena helpers and the
failure result are isolated explicitly; this is not a DSP execution test.
Evidence is `../private/audio-host/effects-failure-original-check.json` and
`check_effects_failure_flow.py`. All disassembly, original keys/code, generated
C, snapshots and game-embedded packages remain private/outside Git.

## Native93–95 evidence and replay

Native93 observes `80004001`, skips the effects getters, and executes the
original cleanup. The real device at `00936000` is released: its worker and
port close before its guest allocation is freed. The original flow then opens
`d:\bink\intro_60.bik`, creates another real device, and reaches the same public
Release wrapper from Bink return `3E3D19`. Its complete caller `3E3C90..3E3D2A`
is 155 bytes, SHA-256
`106f108ee456f5db6b1420b1942fde99dae505bcd738736e9e0dfc4cac198806`.
After checking that caller, native94 releases the temporary movie device,
recreates it, and reaches original sound-buffer creation.

Native95 repeats that exact stop after the diagnostic's input reader was
made safe across noncontiguous guest pages:

```text
entry=0037D4BE return=003E3B97 esp=005E5994
interface=00B76008 description=005E59CC output=8006403C outer=00000000
worker: grains=1 nonzero=0 peak=0 error=0
terminal worker/port close result=0
```

The public buffer wrapper is 36 bytes with `RET 16`, SHA-256
`1e031345f578d8e9a006024dd3965019ce536af7c4010febb53397685aa543f5`.
It remains unsupported. No descriptor fields or PCM format are inferred from
unrelated stack words; a read-only descriptor/format capture is the next step.

GET=PUT=`03B43280`, packet remainder zero, and the stored 960×544 frame1 still
contains only `FF000000`. There is no visible movie/main menu and no nonzero
game audio. These runs reach movie initialization through an explicitly
failed game sound initializer; they do not establish a working audio-free
main-menu route. The preserved lab still reports the refusal to format raw
cache4 while it contains mounted data; this diagnostic does not change or
bypass that existing limitation.

Native94 returned to Vita3K idle. Native95 completed the guest terminal trace,
closed the worker and wrote all snapshots, then Vita3K raised SIGSEGV while
transitioning through its post-exit Stopping/relaunch phase. The root cause of
that emulator shutdown fault is not established by this test. No Halo 2
emulator/build process remains running. Native93, native94 and native95
artifacts and videos are preserved separately under `../private`.

| Native95 artifact | SHA-256 |
| --- | --- |
| ELF | `3dbeb7f4b26504671e252191955dd2baff0f267902498f153ab9b73b35845926` |
| EBOOT | `f32ac00f8cc7bf001ee561ac7990723a29d3c58d80632c116dfe8c62d26183f3` |
| VPK | `1537e5702b11ad61f1b9734d227f659997b472c16d033289d1fdf97c5cd29b34` |
| Trace | `490f474472077cadc0e37c5e4de55f3ebbbf92f6da4ff9128f5d7a8631108dd4` |
| Channel JSON | `a2b3a19912c99b5a3e79150d46afd479d4b945b4679f2a369fa6ec0f4655bf00` |
| Stored first frame | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Generate with the existing `--host-channel --audio-host` preparation flags
into a new private `audio-effects-failure` directory. Build with
`HOST_CHANNEL=1 AUDIO_HOST=1 QUAD_RENDER=1 AUDIO_EFFECTS_UNAVAILABLE=1`,
`GUEST_OPT=-O0` and no more than four jobs. Use a fresh build directory when
changing feature flags. The normal host-audio build omits the explicit
failure option. The shared mixer/output code and all CE paths are unchanged.

For the exact final archived executable, from the existing `../private` lab:

```sh
python3 run_lab.py effects95-review native-95-artifacts/halo2-boot.vpk
```

No controller input or cache reset is required. Collect the trace and snapshots
from `vita3k/ux0/data/xita-halo2/`, then run `python3 run_lab.py stop`. The helper
uses only the Halo 2 title `XH2B00001` and isolated display `:111`. The package
embeds owned game image/code and must not be uploaded as a distributable
release. Generated C, payloads, packages and traces remain outside Git.

The next bounded task is to capture the actual movie buffer descriptor and
wave-format structure, audit its subsequent data/lock/play/release calls, then
connect only a supported buffer route to the already tested real mixer/output.
In parallel with that causal path, a successful original game sound initializer
still requires real support for its DSP image; no substitute effects descriptor
has been created by this work.
