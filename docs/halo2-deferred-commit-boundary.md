# Original deferred sound commit boundary

Native211 adds a read-only terminal snapshot for original DSOUND entry `37D141`, caller `21F201`. It reproduces native210's strict stop without accepting the method. The original Microsoft Game Studios intro is visually verified; the last presented 960×544 frame 136 remains entirely RGB-zero. **The original main menu is absent.**

The public wrapper takes exactly one interface argument and returns with `ret 4`. It rebases the interface by eight bytes, calls `37CD15`, and returns its result. The next stack word is not an output pointer. The internal method updates the listener through `37CA0A`, handles pending listener effects through `37EE86`, walks the original child list through `37A669`/`382032`, and finally clears the listener dirty fields. It cannot be replaced with a general successful no-op.

The terminal probe reads only the checked caller stack and adapter-owned state. It never follows an unverified original device pointer, invokes a sound method, updates a mixer, clears dirty flags or resumes the blocked caller. It bounds all route/filter/spatial dumps by their fixed storage sizes, even if the recorded route count is invalid, and preserves native FP state.

The actual device is `00936008`, with 177 references and 173 children. Guest FCW is `023F`, x87 stack empty, DF clear, native FPSCR `60000011`, and listener dirty bits `25`. Active distance/rolloff/Doppler are `4043126F`, zero and 1.0; pending values are the same distance, zero rolloff and zero Doppler. Pending position is zero and orientation is +X/+Y.

The snapshot contains 66 submix/FX buffer records: 51 inactive submixes and all 15 active FX sources. The three spatial FX23/24/25 records retain the expected fixed geometry. Only spatial FX25 has a pending I3DL2 dirty word (`007F0000`); its complete 164-byte spatial block matches the independently constructed original-code fixture byte for byte. The original single-voice commit on that fixture clears this dirty word without changing parameter/voice state or issuing MMIO. This per-voice finding does not establish the full device commit's behavior.

An exploratory full-device fixture also returns through the original public wrapper, reaches the original listener/child methods and produces no MMIO for the fixed FX25 case. Its listener-effect dirty field is explicitly initialized to zero based on the earlier immediate scalar commits and location-only helper `37A110`; this causal path and the other active/inactive children require complete validation before implementing the boundary. Original Play sets the listener's derived basis bit, producing dirty `65` in the original representation; the adapter's `25` records only the API's pending fields. An initial oracle assertion that omitted that derived bit is preserved as a failed fixture and excluded. A mistakenly named prior fixture tested FX23, not FX25, and is also preserved and excluded; the corrected FX25 fixture uses the actual deferred-input path.

All 54 host executables pass. Four synthetic probe scenarios verify unchanged full guest CPU, native FP, guest memory, device ownership and buffer registry, including invalid stack mappings/alignment and an oversized route count. The adapter ASan/UBSan test passes. Native211 verifies all 180 dependency targets, 130 generated source paths and 179 object identities; only `audio_host.o` differs from native210. Generated code/image and rendering/audio behavior are unchanged. The owned :111 PID is stopped after capture.

Private evidence is `private/sound-query211/`, including `native211-commit-probe.log`, parsed `native211-buffers.json`, original-code fixtures/results and `native211-summary.json`. The exact build and captures are frozen in `private/native-211-artifacts/` and `private/native-211-view/`.

| Native211 artifact | SHA-256 |
| --- | --- |
| halo2-boot.elf | `1d837bd4547a01fccd6bd6d41514cac8f9b05622f282db7c028dcbda3d19f1e0` |
| eboot.bin | `f3a5f40d39ccc36d5e10017bfc729423fe102c41e482c31ba72de0fd2f8f5d27` |
| boot.log | `c27752a31592167f796ba8b13fecfa0b2199c3401701fe7c45285749d944f743` |
| channel-at-stop.json | `eb76b6b5f524242f6a232ddf9985b296f3183f7509c7780b391845b82de11f7e` |
| last-presented-at-stop.bin | `82f9ef8f261e820982a3f362c92587e1178f0c815958458091527532a8e31d54` |

With both owned lab instances stopped, replay the frozen private package using a fresh label from the private directory:

```sh
python3 preserve_fresh_cache.py native211-replay
python3 capture_run.py 211-replay native-211-artifacts
python3 drive_startup.py 211-replay native-211-artifacts
```

Next is a complete original commit audit for the captured fixed listener/source state, including listener-effect dirtiness and every child class. Any supported adapter must preserve active filter histories, source ownership, queued grains and actual DSP time; dynamic spatial behavior remains unsupported until separately implemented and tested.

Owned code, parameters, traces, captures and diagnostic packages remain private and outside Git. The packages embed owned game material and must never be uploaded as distributable releases. Halo CE, the shared emulator and physical hardware are untouched.
