# Halo 2: alternate empty streams reach movie playback

Native138 creates the four empty streams from original `334148` → `336116`.
Their mono16/8000Hz format, two-packet capacity and unity routes27–30 match the
prior streams, but the retained callback is `335D99`. `336116` allocates game
bookkeeping without submitting packets. These objects have real mixer voices
and parent references; their Process and callback execution remain unsupported.

The exact callback is accepted only with one of the four unity routes, not the
five-route muted shape. A private original parameter oracle verifies the stored
callback and format in187 original instruction addresses. All43 host executables
and stream ASan/UBSan pass, including four empty alternate objects, gain, lifetime,
and rejection of the alternate callback with muted routes.

Startup now reaches original Bink movie Play `37B6DF`, return `3E35DB`, buffer
`0180601C`, voice107. It contains real committed stereo16/44100Hz movie data,
106496bytes, headroom0, volume0. The six-bin request received the existing
diagnostic unsupported HRESULT, and the original movie selected FL/FR unity.
The strict stop is **loaded DSP PCM voice routing unsupported**. The existing
movie decoder/sink path predates the real GP loop; these must be connected
before playback can advance. **The frame remains black and no main menu or
nonzero game audio is observed in this run.** The earlier Microsoft intro
checkpoint remains a separate effects-unavailable run.

Native138 observes64 original zero-stream callbacks,70 decoded packets and eight
pending packets at the stop. All170 dependency targets were checked; no generated
code or shared CE behavior changed.

| Artifact | Native138 SHA-256 |
|---|---|
| ELF | `5982a8f460491a43da91c1f340feb80f8b77df515384383d4ddfb853229ba632` |
| EBOOT | `57e8143800a040bf855d49899032cd74f8ad3cfd0df9a63cc9fdbc393ae188be` |
| VPK | `5292c14ed07116ae18dacd1c79def1b0263f63c67e985056e3214a1f4962b7db` |
| Guest trace | `10709fd7e7192f4545b3ed0d7cc1fbd40fec8add6333160817cdceb3ab410f71` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence: native138 artifacts/views/manifest, `audio-stream-alternate`,
`audio-host/alternate-stream-params-original.*` and matching host/sanitizer logs.
Replay from the private directory:

```sh
python3 run_lab.py 138-replay native-138-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

The build uses `audio-stream-process/generated` and image, the existing
DSP/spatial/filter diagnostic flags, and output `audio-stream-alternate/build`.
**The package embeds owned game content and must not be uploaded or distributed.**
