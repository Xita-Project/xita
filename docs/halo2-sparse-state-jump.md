# Halo 2 sparse state dispatch and second-device submission

Native53 executes past the original state transition and reaches a new strict
graphics stop: method `1B20`, value `037CE000`, in the second device's command
submission. It clears 307,200 framebuffer pixels before that stop. The viewed
Vita3K capture is black; no menu or geometry is rendered. The normal profile
still stops at audio hardware. This run explicitly selects the unavailable-audio
diagnostic and creates no sound device.

## Original sparse jump

Native52 stopped at generated code for `18EBDD`, after loading EAX=5 from
`4ED294`. The original instruction jumps through `[EAX*4+18EC08]`. Its six
table entries are `18EBFE, 0, 18EBE4, 0, 18EBE4, 18EBFF`. The table ends at
`18EC20`, where another function starts. The existing generic discovery heuristic
stops at the first null entry, producing a switch that rejects valid later
cases. This stop was not an original assertion.

Only the Halo 2 host-channel hook changes. Preparation checks the original
function and table fingerprints, includes the three non-null executable case
targets, and lowers this one instruction through the existing indirect-tail-jump
path. Execution reads the actual guest table word and adds no return address.
Null or unknown targets still fault. The generic discovery heuristic and Halo CE
emission remain unchanged.

| Guard | Length | SHA-256 |
| --- | --- | --- |
| Function `18EB80` | 100 | `85bfb44438536da9c07ee5e19acdea5a49c29502b71c96b3214736fd4b29902a` |
| Table `18EC08` | 24 | `93324ac5bd2d4025f18ded90bf69be6bdce3f496bd293bc14b2948db69676743` |

Synthetic tests compile and execute both the unchanged default and the opt-in
lowering. They cover null holes, the late case, duplicate targets, out-of-range
and wrapped indices, runtime table mutation, unchanged return memory, exact
stack movement and full other CPU/FP state. The shape and fingerprint guards
reject mismatches. All 31 focused Python regressions pass, including the 12
callback-root tests. The two sparse-jump tests also pass with ASan/UBSan. All 16
host executables passed for the preceding kernel-stack integration; this change
does not modify their runtime code.

## Native 53 evidence

The strict terminal trace is:

```text
PUT=03B54BB0 GET=03B54AA4 result=6 source=03B54AA4
word=037CE000 sub=0 method=1B20 clears=1 pixels=307200
NV2A write eip=003FAC58 address=FD800040 value=03B54BB0 reason=3
```

The decoded channel snapshot logs completion=1 and parses. Unlike native48's
empty second-channel setup, it now holds surface format `128h`, pitch
`0A000A00h`, a 12-slot vertex program, program start 0, execution mode 4, and
constant-load cursor 2. Semaphore release count is 1, at `03C54000`, value 5.
The raw device/push captures still match the earlier queued bytes; the consumer
has now advanced GET into them. No unsupported command is accepted by this
startup fix. Texture state and actual draw execution remain the rendering
boundary.

Private generation is `sparse-state-jump/generated`. Exact binaries and traces
are frozen in `native-53-artifacts`, with screenshots/video and the initial black
scanout in `native-53-view`. The displayed scanout's SHA-256 remains
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.
The emulator was stopped after capture. No hardware or storage device was used.

| Native 53 artifact | SHA-256 |
| --- | --- |
| ELF | `9a29a5ac7cfb62036ad54e83f343c8be6177ae443221f2b4c1c54dcd538b5805` |
| EBOOT | `899bee6469dcaf75645d45dd93827d1ec81855b3a78a0ec11f56218c4f9c2a80` |
| VPK | `c8db62f3cb52c0dc88b0748cf9fab110786e3c417266f03dde77b29b24805e3e` |
| Boot trace | `48f0aea820bc6e010954a31859031aba6fcd2b3b6cd1dec05f46638a84791e09` |
| Decoded state | `b1770974ef525e7a27c0a572c1f86022977da4519a1845588c666b5143f5d27e` |

Replay the exact archived build with an unused label:

```sh
python3 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/run_lab.py replay53-review1 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/native-53-artifacts/halo2-boot.vpk
```

Generated code, game images, shaders, captures and diagnostic packages stay
private and out of Git. The package embeds owned game code/image data and must
not be uploaded as a distributable release.
