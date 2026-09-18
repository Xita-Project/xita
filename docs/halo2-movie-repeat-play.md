# Halo 2: active movie Play retries preserve progress

Native140 passes Bink's original active-loop Play retry at return `3E3639`.
The real movie decoder/GP produces13 nonzero sink grains, peak379, with45056
movie output frames submitted and44032 consumed. **Its three presented frames
remain RGB-black; there is no visible movie or main menu.** The sink sample
measurements do not claim an independently recorded or listened-to audio track.

Original `3E35C0` retries Play if its cursor stays unchanged across50ms. The
private oracle executes291 original instruction addresses for repeat flags1
on the correct A0 stereo44100 format and a controlled already allocated,
mapped, active looping voice. It records reassertion of source/loop registers,
unchanged voice state, and no write to the current buffer offset. A positive
control with flags3 writes that offset to zero. Prior DMA allocation and FIFO
availability are fixture contracts; the audit does not execute Xbox hardware.

The adapter accepts only the observed active retry and unchanged voice/ring/
rate/flags. It checks real playing state and retains all decoder history,
queued grains and consumed progress. It does not call the mixer's restarting
Play helper. Other retries and explicit reset flags remain unsupported.
All44 host executables and buffer ASan/UBSan pass. Full ABI tests confirm
unchanged object/decoder/progress on the retry; the concurrent GP test checks
that a held pre-Play grain remains uncredited afterward.

The next strict native boundary is original Stop `37B703`, return `3E38A1`.
The loaded GP can retain one computed grain in addition to the submitted
grain. Both must be accounted for before reporting Stop, rewind or release.
The older effects-unavailable Stop path is unchanged.

| Artifact | Native140 SHA-256 |
|---|---|
| ELF | `28bb7db360dc84c7ab27f968cfde95a08afd5ceb5993150cc9381d077d56cf59` |
| EBOOT | `d04bf060735d4a929b5bdb3bff8b3dc74768f52c1f42b674726e50762189f7fd` |
| VPK | `85627d3697fb089ee69825a63839ab61ddbc07ddbb357fee1cdd674fa4e4fc27` |
| Guest trace | `ed4d6a20bfb9e537778c11fb2f38b005eac71c9ae12753bd7b6c4f0c9f48a267` |
| Black frame3 snapshot | `9623205268b5aaa402b8859566b1d9f2765d0d18ce997da94777c55139a994b2` |

All170 dependency targets were checked. Private evidence: native140 artifacts,
views/manifest, `audio-movie-repeat`, `audio-host/repeated-movie-play-original.*`
and `dsp-bringup/movie-repeat-*`. Replay from the private directory:

```sh
python3 run_lab.py 140-replay native-140-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

Build uses the prior generated source/image and diagnostic flags, with output
`audio-movie-repeat/build`. **The package embeds owned game content and must
not be uploaded or distributed.**
