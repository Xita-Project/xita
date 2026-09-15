# Deferred reverb writes through the original DSP monitor

`h2_dsp_queue_reverb9` implements the two shadow writes and command publication identified in the [original reverb audit](halo2-reverb-description-audit.md). It supports effect 9's type-12 layout only: a flag word at byte 16 and 66 converted parameter words at byte 280. The original dirty extent is the complete 528-byte region from offset 16 through 543, including the unchanged saved-image gap. The API queues monitor command 2 and leaves live GP memory, histories, registers and frame counters unchanged until the next actual DSP frame.

The function rejects pending monitor work, active/faulted/uninitialized engines, unsupported flags and invalid or incomplete image/state/GP ranges before mutation. Input parameters are copied first, including when their readable host span aliases shadow storage. Raw signed parameter words retain all 32 bits in shadow; the original monitor DMA performs the actual 24-bit conversion. It does not manufacture completion, reset the engine or issue independent immediate writes.

`tools/verify_halo2_reverb_commit.py` runs the original owned setter `37E52F`, range helper `37E39E`, copier `383D79` and commit `37E3C5` under Unicorn using synthetic buffers. No functions are substituted. It verifies the intermediate extents, retained gap, exact published command fields, unchanged source, nonvolatile registers and stack cleanup across 112 distinct instruction addresses. Its output must remain outside the source checkout.

All 54 host executables pass. Dedicated queue fixtures cover normal and aliased sources, every scratch byte outside the intended writes, unchanged full engine state, repeated busy requests, and 22 invalid-state/range/control cases. ASan/UBSan passes the same queue and existing engine checks.

A private test then uses the actual owned DSP image and the parameter bytes produced by the verified original conversion. The original GP monitor imports all 132 expected words exactly once, starting at X word `45B`; it acknowledges command 2 and completes the frame with 104 transfers. The reverb effect processes its update flag from 7 back to 3. This verifies real monitor/effect consumption, not just command parsing.

The same test passes as a native Vita executable on the unchanged isolated :111 Vita3K, title `XH2R00001`. Its result matches the host monitor test: 132 exact imported words, command acknowledged, third frame completed, flags 3 → 7 → 3. Evidence is `private/reverb-queue/native-probe02/`. The first probe used stdout redirection and produced an empty report; it is preserved and excluded. The corrected probe uses an explicit file stream and reports `completion=PASS`. Its owned PID was stopped after completion.

| Native artifact | SHA-256 |
| --- | --- |
| ELF | `670616a37ba166e1e6fd8e0c283f5b93dff08c94ebe45746f4e388d8897636c4` |
| EBOOT | `a6827492a408342d6d464922ffc200d3499f2f1f1fd272a14c215b7733764f20` |
| Result | `3f6b3bf37a4e777bc8e6ff90d4efdcc747a388132a1be127daadd68ddae595bc` |

With both owned Halo 2 lab instances stopped, replay the frozen native utility from the private directory with a fresh output label:

```sh
python3 reverb-queue/replay_probe.py native02-replay
```

This checkpoint does not yet connect the original game's reverb conversion to the queue or the real audio worker. The latest game replay is native204: original Microsoft Game Studios intro visible, last-presented frame 136 black, and strict stop at `XAudioSetEffectData`. **The original main menu is still absent.** Next is the checked original-converter bridge and serialized worker connection, with native startup replay and no substituted sound success.

The private utility/package embeds owned DSP code and converted data and must never be distributed. Owned bytes, generated code, parameters, screenshots and traces remain outside Git. No Halo CE, shared emulator binary or physical hardware changes are involved.
