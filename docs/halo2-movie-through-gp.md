# Halo 2: movie PCM through the active GP loop

Native139 connects the original movie's real decoder to GP inputs0/1 and the
actual output sink. It submits2048 movie-tagged output frames and observes1024
consumed frames before the next strict stop. The first samples in this run are
still zero, and the displayed frame remains black. **No nonzero game audio,
visible movie or main menu is established by this run.**

The original movie selects the existing FL/FR unity route after receiving the
explicit six-bin unsupported HRESULT. Its PCM16 stereo44100Hz ring has106496
bytes, with original Lock/Unlock commits, volume0 and headroom0. The adapter
accepts only that exact loaded-DSP Play caller/state and validates the real
mixer inventory: two muted owners, four zero streams, and the late movie.
Other active voices or altered formats, gains and routing stop before mixing.

The shared decoder's existing16-bit resampling and50% master headroom are
preserved explicitly. This is software audio precision/gain, not an exact
Xbox APU voice-stage emulation. Decoded FL/FR remain separate and enter the
float accumulator before the prior-frame FX contributions, matching the late
movie voice's place in the existing reverse creation order. Signed24 GP
conversion occurs after accumulation; no surround fold or post-GP bypass is
used. Existing spatial/filter histories and the real GP execution continue.

Every computed GP grain now retains whether the movie actually contributed.
A grain computed before Play cannot advance its cursor when submitted later.
Actual sink consumption and the independent decoder frontier still drive the
two reported positions. Loaded-DSP Stop remains strict because its extra
retained computed grain requires a separately tested drain. Terminal close
joins/drains the sink worker before stopping its movie and other PCM owners.

All44 host executables and28 Python tests pass. The synthetic real-GP test
checks separate channels, opposite signs, clipping after accumulation, unchanged
other bins and advancing FX histories. The concurrent real-decoder/GP test
checks exact nonzero left/right inputs, actual output samples, delayed pre-Play
ownership, cursor movement and rejection of an extra playing voice. It passes
ASan/UBSan and ThreadSanitizer. The standalone Vita FX probe also links with the
read-only mixer bridge. These synthetic nonzero tests are not native game-audio
evidence.

Native139 reaches original Play return `3E35DB`, then reports played2184 and
decoder frontier3840 bytes. Bink's original `3E35C0` observes no cursor change
over its50ms retry window and calls Play again, return `3E3639`. That previously
unsupported repeat is the exact next stop. It must be audited against original
active-voice behavior; resetting a live decoder or fabricating cursor movement
would be incorrect. All170 build dependency targets were checked before launch.

| Artifact | Native139 SHA-256 |
|---|---|
| ELF | `38009a7c24189f25e28def9f0d5aa0bae6df9b1ac48c6e3695b8b4c013189b13` |
| EBOOT | `acef04c1caf7df4a60cbbb797db5c45dc26a1fe5512dfc07efde2796b2ea7380` |
| VPK | `0d3486657faf6a04042415a61b0883b620a224debbecd86e5ebe1d6d4648a416` |
| Guest trace | `e832f3bb3efaac652e1f26cbe0c6e878fe7831c39649fbe44a234bebb00c87bc` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence: native139 artifacts/views/manifest, `audio-movie-gp`, and
`dsp-bringup/movie-gp-*`. Replay from the private directory:

```sh
python3 run_lab.py 139-replay native-139-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

The build uses `audio-stream-process/generated` and image, the existing
DSP/spatial/filter diagnostic flags, and output `audio-movie-gp/build`.
**The package embeds owned game content and must not be uploaded or distributed.**
