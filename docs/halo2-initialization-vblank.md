# Halo 2 real initialization vblank and original callback

Native63 releases native62's blocked flip using a real Vita display vblank.
The adapter recognizes only the observed first interval-one initialization
queue, disabled 480p scanout, empty second slot, identity gamma, no swap
callback, and the original game vblank callback `12B2A0`.
Every guest input/output span, callback data span, and callback ring index is
checked before a wait or guest mutation. Other callbacks and states reject.
A failed display wait or unchanged host vcount preserves the pending guest
queue, interrupted CPU/FPSCR state, and rejected parser word.

After a real vblank, the adapter performs the original first progressive
vblank's count/time bookkeeping, then executes original queue consumer
`3FECC0`. Its original `3FF548` and `3FF5C6` calls retire the queued buffer
and apply its gamma data. The adapter verifies real queue retirement before
signaling the original notification event through `KeSetEvent` and calling
original `12B2A0` with the cdecl 12-byte record `{1,1,1}`: vblank count, retired
swap count, and flags. Original `14280` increments the game counter; the
adapter reads and verifies the increment instead of replacing it.
Only then does the parser retry the blocked `FLIP_STALL` word.

All interrupted xctx fields and native FPSCR survive the copied-context
calls. Native FPSCR preservation also now covers the earlier software flip
handler. This is synchronous delivery of the audited callback's integer
body, not a general interrupt controller, IRQ-priority model, continuous
vblank source, or recurring presentation implementation. Analog encoder
field/interrupt ports are not fabricated. The selected progressive field is
zero, matching the already supported virtual progressive mode. Scanout is
still disabled during this initialization.

Native63's real Vita vcount advances `82 -> 83`. Original queue retirement
updates consumer `1BC` to 1, clears queue flag `174`, applies all 768 gamma
bytes and advances the channel's read index. Original callback `12B2A0`
changes game counter `485AB0..485AB4` from 0 to 1. Submission resumes to
GET=PUT `03B44014`, five completed semaphore releases (last value 13).
The next stop is original mode-transition wait `3F9BF7` on event `406D9C`.
The caller has reset its event to zero and requests a further real vblank;
that second wait is not yet implemented. There is still no new presentation,
geometry, main-menu map load, or visible menu. The first scanout remains black,
SHA-256 `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.

All 18 host executables plus the timed-initialization case pass. Both runtime
cases pass ASan/UBSan. Tests cover missing callback pages, invalid ring indices,
host wait failures/no counter advance, no premature retirement/event/callback,
exact callback arguments and order, full interrupted CPU/FPSCR preservation,
and retry of the original parser word only after completion. Synthetic helper
contracts are separate from native63's execution of the original guest bodies.

| Private native63 artifact | SHA-256 |
| --- | --- |
| ELF | `625db49c210557c1ed2644d63678b3b2e9ac7b2b4fd0fe7ce6cbcd8967c84e2e` |
| EBOOT | `1d36fea9ffb07386d09a06a293ae8f900cdc8347eb67d3e751ce2b052789bfba` |
| VPK | `d243ca840e80e4b91ac4bf405b83507d4bd82bb55acf1e1d820928fce22e674d` |
| Boot trace | `8cc8dbef606c6072439a69ef8987afb20a99df6ff263806e32f126acd17e14d4` |
| Decoded channel | `c031753023b336b9b87f56009ab65edce7fe744c78e14ea45b26017dea2e33fd` |

Replay from the source directory:
`python3 ../private/run_lab.py replay63 ../private/native-63-artifacts/halo2-boot.vpk`.
The generated source remains frozen at `online-interfaces/generated`.
The diagnostic package uses the explicit unavailable-audio and limited native
FP probes and embeds owned game code/image. It must not be uploaded as a
release or distributed. All assets, generated code and captures stay private.
