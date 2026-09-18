# Halo 2: two original muted PCM voices in the real GP pipeline

Native130 executes both original low-rate PCM Play calls at `2215B9` and
advances to global DirectSoundCreateStream `37D835`, return `335ED8`.
**The displayed first frame remains black, game audio has no nonzero samples,
and no main menu or gameplay is demonstrated.** This completes the bounded
playback step following [native129 creation](halo2-global-pcm-buffer.md).

The adapter accepts only the observed looping mono8/1000Hz, 1000-byte,
headroom0, volume-10000 buffers routed to bin14 at unity. It validates the
caller, flags, mapped source/mirror and object state before starting the real
shared-mixer voice. Both voices decode and resample normally. Their fully
muted output must be zero before the GP runs; an unexpected sample stops with
error-1007 before submission. Zero is their exact supported GP14 contribution.
This does not implement arbitrary audible PCM-to-GP routing or hardware pitch
quantization. Unmute, active changes and subsequent unaudited methods still
stop explicitly.

The worker preserves the source mask of every prepared grain, so an older
grain cannot satisfy a new Play. Success requires an actual sink submission
containing that source. Separate submitted/consumed counters use the sink's
observed remaining samples. Terminal teardown joins and drains before stopping
these voices and releasing synchronization. The original public Play ABI and
flags/HRESULT forwarding use the previously audited movie wrapper; a private
attempt to execute the entire original low-level PCM manager did not complete
because its fixture lacked initialized manager lists, and is not validation
of the hardware backend.

All41 host executables and28 focused Python tests pass. A new concurrent test
runs the actual shared PCM decoder and synthetic GP through the real worker:
it checks two owners, retained old grains, decoder time, zero GP14 input,
unchanged nonzero GP output, consumption, strict nonzero rejection and drain.
ASan/UBSan and ThreadSanitizer pass this worker fixture. The owned-coefficient
ABI fixture and ASan/UBSan cover full context, Play flags/backend failure and
active method rejection. Synthetic data stays in tests; owned data stays
private.

Native130 creates voices82/83, interfaces `0136601C` / `0138601C`.
Before terminal close their submitted counts are7168 /6144 frames, consumed
6144 /5120; both are playing. All15 FX sources remain active. GP computed /
submitted /consumed frames are31,744 /31,744 /30,720. Compute totals1,960,911us,
maximum grain70,912us; error, peak and nonzero grains are zero. The GP worker
is still slower than real time; queue/deadline counters are observations,
not hardware interrupt emulation. Close and all snapshots complete.

All169 dependency targets were retargeted and verified in the isolated
`audio-gp-pcm/build`; unchanged generated code comes from `audio-global-pcm`.
No shared CE mixer source changed.

| Artifact | Native130 SHA-256 |
|---|---|
| ELF | `1c1b058d6dca42ba31d85a68899bf5f70a7270bd3acd88175b4b8c35c17ee90f` |
| EBOOT | `c74c880ff48e33b866277f18f2c911ccdb235054c593f5138686c65a13aa77c1` |
| VPK | `ffce604f6cca83862b5151b08c20550661ba9b6e9fc5c9c65152f611c500a2da` |
| Guest trace | `ed7d8fd76235384ec98c4cfca79e373397459a0a3b4b863830432e45530ef736` |
| Black scanout | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

Private evidence includes native130 artifacts/view/manifest,
`dsp-bringup/audio-gp-pcm-*` and the isolated build. Replay from the private
directory:

```sh
python3 run_lab.py 130-replay native-130-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

Build uses native129's generated image and real DSP/spatial/filter options.
**This diagnostic package embeds owned game content and must not be uploaded
or distributed.** The next task is a read-only descriptor/caller capture and
original wrapper audit for global stream creation; its flags already differ
from the earlier instance stream path.
