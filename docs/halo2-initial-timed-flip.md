# Halo 2 first interval-one flip

Native61 reaches software selector 1 with argument `038E8001`, encoded as
`71D00021`. This is the second device's first flip: interval one, non-immediate,
with scanout still disabled. The first device instead used an immediate,
interval-zero initialization flip. The original `3FF240` handler sets the queue
entry's due vblank to 1. Its `3FECC0` consumer compares that due count with the
actual serviced vblank counter, currently zero, and leaves the entry pending.
This follows the original code and preserves its timing requirement.

The adapter validates the existing empty-queue/disabled-scanout input contract,
executes the original handler on a copied CPU context, then checks the pending
entry and unchanged gamma/read-counter/framebuffer state. It does not perform
a display wait, run the vblank callback, retire the entry, or present a frame.
A second uncompleted initialization flip is rejected. Unsupported timing,
client swap callbacks and resources still reject before guest mutation.
The client vblank callback is retained in guest state for future delivery.

Native62 records the original queue at miniport offsets `174/178/17C` as
`1/1/038E8000`, producer `1CC=1`, consumer `1BC=0`, actual vblank `1C0=0`,
next requested count `1C4=2`, and gamma pending `7DC=1`. The next original
`FLIP_INCREMENT_WRITE` changes write index 1 to 0. With read index still 0,
`FLIP_STALL` correctly stops at source/GET `03B43FC8`, PUT `03B43FF4`.
No completion is fabricated to release this wait. The captured callback at
miniport `190` is `12B2A0`; it has not executed through this bridge.
The first scanout remains black, SHA-256
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.
There is no main-menu map load, geometry, or visible menu.

All 18 host executables plus a separate timed-initialization contract case
pass. The runtime test runs both cases under ASan/UBSan. Synthetic helpers
exercise the call ABI and full interrupted CPU-state preservation; native62
separately executes the original handler and validates its actual queue writes.

| Private native62 artifact | SHA-256 |
| --- | --- |
| ELF | `b8f8bcf7151108d2712d5eebc77701700cfd277b3c9f75facf3600f9d1ce433a` |
| EBOOT | `e4e55604a7f843bdadd553fc5b9b06abea2ca8e0f102d4a0e97100d5d1f47add` |
| VPK | `b367dfda375c7b6cc46819c0f4e6c1487e25cddecef511e4f0a05d724d27e7ea` |
| Boot trace | `3f4ff4726e941a17b4eff4bf090d887274ac962778efccf26a17e933668e2326` |
| Decoded channel | `8451364953b34b28c1455c1d09d1806bdd3144bcd8543c9fb30894ce82473d4c` |

Replay from the source directory:
`python3 ../private/run_lab.py replay62 ../private/native-62-artifacts/halo2-boot.vpk`.
Generated source remains frozen at `online-interfaces/generated`. The package
uses the unavailable-audio and limited native FP diagnostics, embeds owned
game image/code, and must not be distributed or uploaded as a release.
The next boundary is delivery of a real host vblank, original queued-flip
retirement, event signaling and the original client callback before retrying
the blocked command. This is not a general interrupt controller.
