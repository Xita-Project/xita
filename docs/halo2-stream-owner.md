# Halo 2: real empty stream ownership

The [native109 descriptor capture](halo2-stream-descriptor.md) establishes the
first stream creation call. The H2 audio adapter now allocates an actual shared
mixer stream voice for each supported object, retaining its format, callback,
opaque context and two-packet limit. It accepts the exact `2AE692` call with
`NOMERGE`, no aggregation, no mix-bin list and original callback `220730`. Original
format builder `21E410` supplies mono/stereo Xbox ADPCM or stereo PCM, all 44.1 kHz.
Every format field and extension is checked before using the shared mixer parser.
Other flags, codecs, callbacks and layouts stop explicitly.

These voices are initially empty and inactive. Guest packet submission,
completion callbacks, stream state queries, dynamic formats and DSP routing are
still unsupported. Synthetic tests feed the real mixer directly to validate its
codec/gain capability; this is not evidence of original game stream playback.
Existing movie-buffer support is unchanged, and loaded DSP playback remains
blocked until it has a supported route. No placeholder sound object is published.

The original constructor `37C9E5` gives the public stream its `417170` vtable and
the common header at offset 4 its `417160` vtable; references reside at offset 8.
Its `37BE27` parent constructor retains the device through `37B16F`; the common
destructor `37A56E` releases that retained parent. The adapter mirrors one owning
device reference per stream, separate from public AddRef/Release counts. Final
Release frees the actual mixer voice under the mixer lock before releasing its
guest page and device ownership. Allocation failure publishes nothing and rolls
back the unused candidate page. Nonempty playback cannot be released through this
bounded path.

Original `37CDCA` establishes 600 hundredths of a decibel of headroom for these
nonspatial streams. The observed `37B818(stream,0)` call delegates to `37A629`,
which replaces the headroom and adjusts total attenuation by old minus new. The
adapter applies that actual zero-dB gain to the mixer voice. Other headroom
requests and volume controls remain unsupported here.

Seven fingerprinted entries in `417170..41718B` are added as roots only for the H2
audio-host profile. Unknown interface methods retain their strict original-code
guards. This bounded table discovery preserves original call/dispatch behavior.

Validation includes all 35 host executables and additional channel modes,
ASan/UBSan for the new stream fixtures, and 25 audio-hook/callback-root Python
tests. The stream fixtures use actual shared mixer voices and split synthetic
ADPCM/PCM data across packet boundaries. They verify codec setup, gain changes,
complete context/FPSCR preservation, parent/public references, guest/physical
aliases, malformed formats, sink/shutdown failure, allocation and mixer-capacity
rollback, and final cleanup. A private 104-case original-code oracle checks
AddRef/Release and headroom writes, exact stack returns and shutdown behavior;
kernel locking, destructor delegation and hardware gain delegation are isolated.

All owned bytes, generated source, packages, oracle programs and traces stay in
the private artifact area. Halo CE and its default runtime are unchanged.

Native110 allocates 81 original ADPCM stream voices (40 mono, 41 stereo) and applies the original
zero-headroom calls. It reaches original game callback `2AE230` through object
`47F0D0` / vtable `45711C` at caller `2AE66B` (return `2AE66D`). That callback was
not yet a discovered root, so the run stops there with ESP `005E5EAC`; no packet
is submitted and only the initial black frame is displayed. Native106 remains
the visible intro checkpoint, and no main menu is shown here. Native110's terminal
capture is complete. Vita3K subsequently hits the same application-teardown
SIGSEGV/access `2008C8` seen in native109; no H2 emulator remains running.

| Private native110 artifact | SHA-256 |
|---|---|
| ELF | `6fd9f18c1ba093b16bfb8d6855f75a564466237fa31985e434a9ccf423ca596b` |
| EBOOT | `badc9d8f4a531d5130ddad7541c6467f5b65e91cc2b498ff78c57ba102a60ba2` |
| VPK | `a6842cb19a1c17edd3e7c623f3e96656e1fec6415fd0f01e423ab5fd4c9bbe83` |
| Boot trace | `e38d429f681fb7dda40fd7c18f3f8fbe57c483c5a7d45fcb8b6fff5410deaee0` |
| Last scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence: `../private/native-110-artifacts`, `native-110-view`,
`native-milestone-110.json`, `native110-host-tests.log`, and
`audio-host/stream-san.log`, `check_original_stream_refs.json`.
The existing [DSP build options](halo2-dsp-game-init.md) apply. Replay this exact
archived package with
`python3 run_lab.py 110-replay native-110-artifacts/halo2-boot.vpk` from the private
directory, preserving prior traces and using only the owned `:111` lab. Packages
embed owned game/DSP code and must not be uploaded or distributed. The next task
is bounded game sound-object vtable discovery, followed by the next observed
sound or rendering boundary.
