# Halo 2 timer ABI and original deferred callbacks

Native56 executes the original timer callback `332F4C` six times and continues
through the preference-file worker and network startup. The next strict stop
is unsupported `STMXCSR` at `535A2`, function `53580`, ESP `005E5F90`.
The viewed Vita3K image and first scanout remain black. The game has not loaded
the main-menu scenario or rendered geometry.

## Why the earlier continuation was unreliable

Native54 caught the shared `KeSetTimer` implementation interpreting the inline
deadline's low word as an address inside the protected kernel-stack window.
Original `332B7A..332B8E` pushes DPC, high deadline word, low deadline word and
timer. The four-word ABI agrees with the
[pinned Cxbx kernel implementation](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/EmuKrnlKe.cpp).
The old implementation dereferenced that low word and consumed only three
words, making its later control flow depend on the changing time value and
an unbalanced stack. Native53 remains an actual captured graphics stop, but
does not prove correct startup through that timer call.

The same audit found that shared DPC initialization stored routine/context at
`+4/+8`, inside the list entry. The actual 28-byte KDPC uses `+0C/+10`, with
system arguments at `+14/+18`. The 40-byte KTIMER layout and initial header
fields are corroborated by the
[pinned type definitions](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/common/types.h).

## H2-only service

Only the separate host-channel target wraps timer/DPC exports. SetTimer consumes
four argument words; SetTimerEx consumes five, with period before DPC. Absolute
deadlines are converted from the actual wall/monotonic clock pair; negative
deadlines are relative intervals. No time value is dereferenced or pinned.
The queue tracks real insertion, cancellation, signal and rearm state. Periodic
timers schedule their next interval from expiration. Notification timers retain
their signal when consumed; synchronization timers reset it. Callback arguments
contain the actual system time at expiration, matching the
[reference expiration path](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/EmuKrnlKi.cpp).

A dedicated cooperative guest worker owns a real stack/context. It queues and
coalesces DPCs, then runs the original routines with four arguments at dispatch
IRQL. It restores its CPU/FP context and prior IRQL after balanced return; guest
memory changes from the callback remain real. Cancellation does not silently
remove an already queued DPC. PASSIVE/APC/DISPATCH IRQL transitions preserve the
actual prior value; device interrupt levels, blocking or preemption at elevated
IRQL stop explicitly. This is a cooperative timer service, not a hardware
interrupt controller or a complete NT scheduler.

Mapped argument/object/routine spans and record ownership are checked before
mutation. Unmapped spans, invalid periods/types, clock overflow, unknown objects,
queue/record exhaustion, modified DPC headers and unbalanced callback stacks
remain strict faults. There are at most 64 timer and 64 DPC records. Shared
timer due polling is disabled for these owned objects; actual expiry belongs
to this worker. Absolute deadlines use the clock pair sampled at arm time;
later system-clock adjustments are not modeled by this milestone. Guest raw
kernel list traversal and device-level interrupt scheduling remain unsupported.

The shared kernel changes only two optional hooks: owned timer consumption and
elevated-IRQL yield checking. With those hooks absent, ordinary targets retain
their existing behavior. Halo CE links neither this service nor its wrappers.
The high-alias kernel-stack query limitations documented in
[kernel stacks](halo2-kernel-stacks.md) remain explicit; no guard is relaxed.

## Validation and native evidence

All 17 host executables and 31 Python regressions pass. Timer tests also pass
ASan/UBSan. Synthetic fixtures cover deadlines spanning the protected high
address range as values, exact stack/CPU/FP preservation, all real callback
arguments, no early delivery, rearming from callbacks, periodic and one-shot
behavior, queue coalescing/removal, notification/synchronization consumption,
mapped-span rejection, invalid periods/types, overflow, stack imbalance and
blocking rejection. Native55 first exercised original DPC dispatch; native56
adds the real prior-IRQL return behavior and reaches the same instruction stop.

The corrected return size leads through different original startup code from
native53. Native55/56 have not yet exercised the new palette descriptor
handling. Preference writes stay within the isolated H2 lab. No source-card,
Vita or original game-file writes occur. The instruction-stop handler currently
does not take a new graphics snapshot, so no prior channel JSON is relabeled
as native55/56 state.

Private generation remains `sparse-state-jump/generated`; artifacts are frozen
in `native-56-artifacts`, captures in `native-56-view`. The emulator is stopped.
First scanout SHA-256 is unchanged:
`a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1`.

| Native 56 artifact | SHA-256 |
| --- | --- |
| ELF | `7f28039acb821c9743fa5ca80cfedf000faea84516796a8608d67964c51e5e8d` |
| EBOOT | `d2bca63f054896df46fd679b30d207bcd475c36d993dda7b92f2231ff397d864` |
| VPK | `cc704eb0be7353cfbee7de431a5b1ae39b81d3170ed8064ebae35074ad75a76a` |
| Boot trace | `40690635bc1d9a1652414a9710878eb2e19337ef9911ea6d7b2fdd434bf97b9a` |

Exact replay, with an unused label:

```sh
python3 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/run_lab.py replay56-review1 /home/birchwoodgod/xita-backups/2026-09-12-halo2-initial-profile/private/native-56-artifacts/halo2-boot.vpk
```

The package embeds owned code/image data. Keep it, generated C and captures
private and out of Git; do not upload it as a distributable release. The run
continues to select the explicitly unavailable-audio diagnostic, not working audio.
