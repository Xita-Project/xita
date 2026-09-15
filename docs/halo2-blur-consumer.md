# Original 160×120 averaging command consumer

`BLUR_RENDER=1` enables only the exact first averaging pass captured in native191.
It requires `THRESHOLD_RENDER=1` and remains off by default. The private contract
pins complete retained setup/validity state, seven program slots and the four
original 24-word vertex packets. No contract value supplies an emitted command.
All six draw consumers preserve exclusive ownership of active primitives and
restore native FPSCR. Unknown operations and changed vertex words reject.

Independent guards require the observed 160×120 ARGB output, pitch640, full
clip and RGBA mask, disabled blend/depth/stencil/fog/alpha testing/stipple, and
four 160×120 linear/clamp ARGB inputs. BEGIN and END validate every complete
DMA span. Read-only inputs may alias one another; the output must be writable
and disjoint from each input in both physical and returned host memory. The
inactive depth descriptor is unused. Failed rendering and returned staging
aliases leave the guest destination unchanged. Only successful END commits.

GXM uploads actual guest image bytes and original vertices, executes the
three-stage averaging shader and waits for completion before returning private
RGBA staging. The original 640×480 window quad remains clipped to160×120.
The RGB and amplified-alpha precision bounds are explicitly measured in
[the GPU probe](halo2-blur-probe.md). There is no new presentation call and no
substitution of black or synthetic images for live inputs.

All 51 host executables and both new consumer/dispatcher ASan/UBSan checks pass.
Tests cover strict packet/state rejection, output permissions, truncated and
misaligned mappings, physical and host aliases, inactive-depth isolation,
failed staging and exact guest-byte commit. The six-consumer dispatch oracle
checks exclusive success/rejection ownership and native floating-point controls.
The read-only alias fixture distinguishes equal-sized inputs from the output:
a host output alias is rejected, while overlapping input views are accepted.

With the averaging option disabled, both `quad_gxm.o` and
`host_channel_runtime.o` are byte-identical to native191.

The diagnostic package embeds owned game code/image and shaders and must not
be uploaded as a distributable release. Owned inputs, generated code/contracts,
packages and traces stay private and outside Git. CE, shared Vita3K and physical
Vita remain unchanged.

Native192 completes the original averaging END at `03B7D038`, committing
19,200 black RGBA pixels from `02B1B000` to `02B31800`. The next pass begins
with the same retained setup/validity and program; only its four sampled
addresses change to the just-completed output. That captured input contains
exactly 76,800 zero bytes. The consumer then rejects method `1A40` at
`03B7D2E0`, value `3F200000` (0.625), because its contract currently authorizes
only the first pass's0.5 offset. The active primitive retains ownership of the
rejection. PUT remains `03B80158`, EIP `003FAC58`.

The Microsoft Game Studios intro was visually checked again. The diagnostic
stop returns the app to Vita3K's library before the terminal screenshot; the
last actual presented game buffer remains black frame135, SHA256
`20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.
**The original main menu has not appeared.** Normal Start followed the full
original 59,670,016-byte map copy. The owned emulator was stopped after archiving.

All177 dependencies were checked. ELF SHA256:
`e6c40cec0e16777ebc6f6fcced81c2305908d0de1dc66bad3f38d079834f19fc`;
EBOOT `280a9ceb1f949d7c1f74f579cfde509635a3b158dfed59d0fa307786ff09ee91`;
trace `d612a79f569e50c3fbe2963111f2f8967a8a92d326db8e8678f6c598537bed6b`;
channel `603969f37cdcde12ab6795527ccb0757c7669bcc1ed252d9918b8d7a99d32d9e`;
push `d703be513d41dec9986660bed755d70117630ca9961129e5811559533371624a`;
next texture0 `34e67af5fd6ea6178034740e4d62bf547f6d96e7f5a166cbb904b0a1ff32543b`.
Private build/tests are in `blur-consumer/`, captures in `native-192-artifacts/`
and `native-192-view/`. Frozen replay from the private handoff directory:

```sh
python3 preserve_fresh_cache.py native192-replay
python3 capture_run.py 192-replay native-192-artifacts
python3 drive_startup.py 192-replay native-192-artifacts
```

Next is the bounded original sequence of remaining filter offsets, preserving
complete packet-pattern consistency and original input/output ownership.
