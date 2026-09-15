# Original sound work boundary

Native201 retains the native200 strict stop at `37B844`, return `21EC3C`, after the first original post-Start frame. The original Microsoft Game Studios intro is visually confirmed. Last-presented frame136 remains entirely black and **the original main menu is absent**. The new probe only records terminal state; no sound work or guest control flow is changed.

The owned wrapper takes zero arguments and returns void. The word at ESP+4 is the caller's saved EBP, not a parameter. It enters the original critical section through `379E9E`, loads singleton `[387198]`, calls internal `37A4AA` if nonnull, and leaves. The internal routine invokes `37EBEB` unless `[386B0C]` suspends work. This matches the pinned [DirectSoundDoWork declaration](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/DirectSound/DirectSound.hpp) and the Xbox symbol database's low-priority service identification; the actual owned instructions establish the revision-specific behavior.

The low-priority routine detaches the current task list at hardware-object offset4B0, dispatches that snapshot in order through each owner's vtable slot24, requeues repeating tasks, and clears the queued bit on one-shot tasks. Work added during a callback remains for a later invocation. A failed DSP canary can invoke hardware reset `37E9AB`. This is meaningful behavior; an unconditional success/void stub would hide required processing.

`tools/verify_halo2_audio_work.py` executes the original work body and list helpers under Unicorn with synthetic task records. Only the interrupt guards and synthetic callback are isolated; the DSP canary is explicitly valid. All32 cases pass, covering empty/nonempty orderings, one-shot/repeated flags, work appended during callbacks, two-argument callback ABI, ESP and nonvolatile registers. It executes87 distinct original instruction addresses. This proves the queue contract, not a functioning APU or audio completion timing.

```sh
python tools/verify_halo2_audio_work.py /private/owned/default.xbe \
  --out /private/new-work-oracle
```

The native probe confirms original singleton0 and suspended0; the real H2 host device is00936000 with177 references and173 children. All82 ordinary streams have submitted no packets. Four accurate-notification streams are actively completing through the existing real sink-gated guest worker; nineteen other accurate streams are empty. Flags20000000 and40000000 match NOMERGE and ACCURATENOTIFY in the pinned [type definitions](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/DSOUND/XbDSoundTypes.h). No ordinary callback is fabricated, and the pending listener dirty state25 is retained.

The next bounded task is to prove and guard the currently empty low-priority host-work case while preserving the original wrapper. This must reject ordinary pending packets, unsupported timed/deferred operations and inconsistent ownership, keep accurate callbacks on their existing worker, and retain strict stops before broader sound behavior. The null hardware singleton alone is insufficient evidence to grant every future call.

All54 host executables pass after the read-only probe. The build verifies180 dependency targets; among179 existing objects only `audio_host.o` changes. The generated code, other renderers/audio backend and shared CE paths are unchanged. Private evidence is in `audio-next200`, `native-201-artifacts`, `native-201-view` and `native-milestone-201.json`. The owned :111 PID3550778 was stopped after capture.

ELF SHA-256 is `4d4b087be908808fe3e40b3e9745464d055a6986b70e45d101d30324a6a09db9`; EBOOT is `6984a2506e1e0e8af2f5f5cb06f98f321d7d325d6b333af6b7049dba1c3abb12`. Trace SHA-256 is `7b224efbf451426b53fccbb19fd060a65583e4b9ac2e891c46a357a3d5aa7a5e`; channel snapshot `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` and last-presented frame `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` match native200 exactly.

Exact private replay with the owned emulator stopped:

```sh
python3 preserve_fresh_cache.py native201-replay
python3 capture_run.py 201-replay native-201-artifacts
python3 drive_startup.py 201-replay native-201-artifacts
```

Diagnostic packages embed owned game code/image and must not be uploaded as distributable releases. Owned executable data and all probe artifacts remain outside Git.
