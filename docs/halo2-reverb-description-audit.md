# Original reverb-description boundary

Native203 passes the original sound-record predicate, then stops at `37BA6F`, called by `21ECE0` with return `21EE74`. The three arguments are effect index, description pointer and optional raw-description output. The last argument is not a flags field. The owned wrapper ends in `RET 12`. Pinned [Cxbx's declaration and explicitly unsupported implementation](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/DirectSound/XFileMediaObject.cpp#L242) corroborate the public name `XAudioSetEffectData` and three-argument shape, but supply no conversion implementation.

Native204 adds a terminal read-only probe, retaining that strict stop. It captures the complete type-12 input and the original 280-byte effect-state prefix through the existing locked backend reader. Every guest span is validated before reading. The probe reads split-page descriptors through `x_guest_read`, preserves the host FP control state, and never writes caller memory, queues DSP work or resumes execution. Seven synthetic scenarios verify full guest/context/device preservation, split-page input, unsupported type/index, unavailable state and invalid mapping/control. All 54 host executables and the audio DSP adapter ASan/UBSan suite pass. The initial split-page header test caught a direct load; its failed build is recorded privately and was never launched.

The actual call uses index 9, type 12 and null raw output. Its description contains room and room-HF levels of -6400, rolloff 0, decay time and HF ratio 1, reflections and reverb levels -6400, both delays 0, diffusion and density 100, and HF reference 5000. The twelve-field order matches Microsoft's [DSFXI3DL2Reverb definition](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416838(v=vs.85)). These are observed inputs, not replacement defaults. The state capture has all 70 words, including the existing live DSP state.

The original type-12 branch reads the prefix with `37A34F`, copies the twelve description fields, and calls converter `3838A4`. It then sets flag bit 2 at effect offset 16 with a deferred four-byte write, defers 264 converted bytes at offset 280, and calls `37A3AE` to commit. The deferred writer `37E52F` copies into the saved image and accumulates a contiguous dirty extent. `37E3C5` publishes monitor command 2 and resets the pending extent. Therefore, replacing this with two independent immediate writes would miss the original span/monitor behavior; implementation must audit the shadow gap and actual monitor consumption. No adapter for this call has been enabled in native204.

A private original-x86 versus translated-C comparison executes the captured descriptor/state through converter `3838A4` and its thirteen directly called helpers. The complete 544-byte workspace matches byte-for-byte, nonvolatile registers and ESP match, and input memory is unchanged. Original writes stay within the checked workspace/context/stack. It executes 717 distinct original instruction addresses. This is one captured conversion fixture; it does not yet validate arbitrary reverb descriptions, target ARM floating-point behavior, deferred writes or live monitor consumption.

The original Microsoft Game Studios intro is visually confirmed. Normal Start follows the complete 59,670,016-byte map copy. Native204 stops at the same API; channel and last-presented hashes match native203. The full RGB image of frame 136 is still black, and the terminal window has already returned to the emulator library. **The original main menu is not visible.** The transition recording lasts 76.8 seconds.

All 180 build dependency targets and 130 generated source paths were checked. Only `audio_host.o` differs among 179 existing objects; generated original code, graphics and mixer remain unchanged. Private evidence resides in `native-204-artifacts/`, `native-204-view/`, `native-milestone-204.json`, and `sound-next203/native204-input-state.json`.

| Artifact | SHA-256 |
| --- | --- |
| ELF | `8d6186613f6ef9777658bfb61d5e14ed2d44810a42738a81242a7c40495495eb` |
| EBOOT | `8c04832ed68105f4d614dd403b8102ce86f822c0b9f4da40d1fea028085e6f13` |
| Trace | `ef06438d4af169b6d3165d79b0ac0820f9e1ff6aa8547cd3dc7dc6bae99eaaf7` |
| Channel | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| Last presented frame | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

Exact replay from the existing private directory with the owned emulator stopped:

```sh
python3 preserve_fresh_cache.py native204-replay
python3 capture_run.py 204-replay native-204-artifacts
python3 drive_startup.py 204-replay native-204-artifacts
```

Owned code/data, converted parameters, traces, screenshots and diagnostic packages stay private and outside Git. The package embeds owned game code/image and must never be distributed. This audit makes no CE, shared-emulator or physical hardware changes.
