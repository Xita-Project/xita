# Halo 2: zero PCM packets and sink completion

Native136 creates the four original mono16/8000Hz streams routed separately
to GP27–30 and submits their eight initial 320-byte zero packets. The actual
shared decoder retires all eight. Startup stops at another global stream
creation with five routes, 27/28/29/30/2, all at volume -10000. **The displayed
frame remains black, with no main menu or nonzero game audio. No guest packet
callback ran before this native stop.** This follows
[global stream creation](halo2-global-pcm-stream.md).

The owned original Process wrapper at `37AD25` has RET12. A private oracle
executes its queue checks, packet copying and pending-output writes, isolating
only page pinning and the hardware kick. Success enqueues without a synchronous
callback; a full queue returns `88780032` with packet outputs unchanged. A
separate original completion dispatch writes completed size/status before
calling `(stream context, packet context, status)`. These paths execute 203
and 56 original instruction addresses respectively; they prove the ABI and
queue behavior, not hardware completion timing. The 119-byte wrapper SHA-256
is `7a182f2613dd476b6f5c86e101220a58cc389291b4acf5f4fce67aa9775d3bc4`.

Native133 captured the exact first packet: buffer `82455D40`, size320,
null completed/status/timestamp pointers, context0, all bytes zero. Native134
accepted the first pair, then stopped on route28. Native135 accepted two pairs
and stopped on route29; its original `333330` frame contained four unity routes
27–30, matching the original four-element constructor loop. Native136 passes
all four. Its later five-route call uses a different frame layout: the old
diagnostic's `four_routes` label is not a valid decoding of that frame. The
actual descriptor's separately captured five pairs are valid.

The adapter is restricted to those exact zero packets, callers `33610E` and
`335D7B`, callback `335D82`, packet contexts0/1 and retained source addresses.
It validates mapping, aliases and allocation before queuing an immutable owned
mirror into the real decoder. Full queues return the original HRESULT. Other
formats, nonzero input, packet output pointers and active stream release/flush
remain strict stops. No guest source memory is fabricated or cleared.

A read-only H2 mixer bridge distinguishes encoded read-ahead from source frames
actually delivered to the resampler. A packet can retire only after both
interpolation lanes pass its end, or the final source drains. Its mirror and
notification remain pending until the containing real output grain passes
the existing Vita sink consumption fence. The cooperative guest worker then
executes the fingerprinted original callback at IRQL2, restoring guest context,
IRQL and native FPSCR afterward. Guest code never runs on the native audio
worker. This is conservative 1024-frame sink granularity, not an emulation of
Xbox accurate-notification interrupt timing. Current GP processing is slower
than real time; completion timing must not be described as hardware accurate.

All43 host executables and28 Python tests pass. Cursor tests use the actual
decoder and demonstrate its read-ahead gap. ABI tests cover malformed packets,
allocation/thread failure, immutable ownership, capacity, release rejection,
sink-gated notification, stack/IRQL and full context restoration. A concurrent
worker test runs four simultaneous streams with two packets each, checks unique
tickets and per-stream order, holds the sink to prevent early notification,
and rejects a fifth voice. Packet and worker ASan/UBSan, cursor ASan/UBSan, and
the four-stream worker ThreadSanitizer runs pass. Native136 validates submission
and decoder retirement; original callback execution remains to be observed.

Only generated `code_105.c` changed;131 other files were compared byte-for-byte
before reuse. All170 copied dependency targets were retargeted and verified.
Native135 additionally rebuilt all active runtime objects from a serialized
clean runtime-object state; native136 records its own complete dependency and
build identity. Shared CE sources and build defaults are untouched.

| Artifact | Native136 SHA-256 |
|---|---|
| ELF | `8ab762bfda7f41f43bd18187c0f0ca44a700406927d8f9c78b5013cf4aeccd25` |
| EBOOT | `4cd5136e706307f79e81fb371b8b64b25b80a9bb1d8944b6dbe4da149be5113a` |
| VPK | `9ae62bbfd62628ae344cf668a24920f58c4ee02831b510a8952903c258ab20cd` |
| Guest trace | `7169622b737f9d3f444c6d36593fc6f56640a27a2bbf49276507563ae14bd785` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence includes native133–136 artifacts/views/manifests,
`audio-host/stream-packet-original.*`, `audio-stream-process`,
`audio-stream-four/native-build-identity.json`, and
`dsp-bringup/audio-stream-*`. Replay from the private directory:

```sh
python3 run_lab.py 136-replay native-136-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

Build uses `audio-stream-process/generated` and its image, with output
`audio-stream-four/build` and the existing DSP/spatial/filter diagnostic flags.
**The package embeds owned game content and must not be uploaded or distributed.**
The next bounded task is the observed five-route muted stream, followed by a
native test of original packet callbacks and subsequent menu initialization.
