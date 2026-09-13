# Halo 2: original initialization flip reaches display-mode application

Native attempt 30 completes the original software flip handler and reaches
`AvSetDisplayMode` after a real Vita vblank. It stops before applying the mode.
**No framebuffer has been presented and no Halo 2 menu is visible.**

## Executed initialization path

The first selector-1 command is an immediate, interval-zero flip while the
driver explicitly disables scanout. The adapter validates the complete guest
stack, miniport state, framebuffer range and canonical color region before
calling the original `3FF240` handler. Its original `3FECC0`, `3FF548` and
`3FF5C6` helpers execute too. A copied CPU context preserves all interrupted
integer, FP and control state; the diagnostic current-function value is restored.
The bounded body cannot yield midway. Guest queue and global writes remain real.

Private execution probes using the captured device state verify one read-counter
increment, queue producer/consumer counts zero to one, global `408650` becoming
`039D0000`, and all 768 emitted identity-gamma DAC bytes. The scanout-disabled
branch performs no framebuffer-address MMIO write. The host bridge accepts only
this checked initialization shape. Client callbacks, queued/timed flips,
non-identity gamma inputs and enabled-scanout paths still reject.

The command consumer implements modulo-checked FLIP_INCREMENT_WRITE and permits
FLIP_STALL only when read and write counters already differ. An unsatisfied stall
stops; it does not manufacture completion. These conditions match the primary
[xemu methods](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pgraph/pgraph.c)
and [FIFO wait condition](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/pfifo.c).

Attempt 29 completed that initialization but blocked waiting for the miniport's
vblank event. The new H2-only wait wrapper services exactly the first audited
mode-transition wait. It calls Vita's real display wait and requires the host
vblank counter to advance. With an empty queue, progressive mode and no client
callback, it updates the driver's count/RDTSC timestamp, signals the actual guest
notification event through `xk_KeSetEvent`, then invokes the original kernel wait.
Other object waits pass through. This is not a general IRQ/DPC or recurring
vblank implementation. Host failures, unchanged host counter and unsupported
guest state reject before guest mutation. Shared kernel behavior is unchanged.

## Native evidence

Attempt 30 completes PUT = GET `03C2BB28`, with one depth/stencil clear, 307,200
pixels and four real semaphore writes, most recently value 11. The real Vita
counter advances 24 to 25 and the wait on guest event `00406D9C` completes.

The next call, at return address `3F9C0F`, is:

| Argument | Value |
| --- | --- |
| Register base / step | `FD000000` / 0 |
| Mode / format | `88070701` / `12` |
| Pitch / framebuffer | 2,560 / `039D0000` |

The primary [mode table](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/EmuKrnlAvModes.h)
identifies this as 640-by-480 progressive output. Format `12` is linear
A8R8G8B8. The current adapter rejects mode application at this exact boundary.

| Artifact | Attempt 29 SHA-256 | Attempt 30 SHA-256 |
| --- | --- | --- |
| ELF | `fa069fc716a4409a3ee0ed563b788d91e5cf959ccf2c78b51e0edcfb2c32cf85` | `eb29e5cc555f3b84b36c7a09bf8a2913e47a8d79a046c99eff240c3a5eccaeeb` |
| EBOOT | `1e1b763ffc4e7573c97534368a2bff6c7cbe3f99213ea0927b26a3368194493b` | `893f40c186695e68c79e9565ad3e33f330e3962627aaea06006b785fa8f22345` |
| Boot log | `9ea31b747677a04602b06161bb4766a14fdcf742a40492b855b31fc8cdd8a8a9` | `15871954466e25401f7df9121260b5cc08a35207217304cb662ba60f88428c54` |

Full manifests are private in `private/native-milestone-{29,30}.json` and
corresponding artifact directories. Attempt 29 generated no fresh snapshots;
attempt 30 has fresh device/push snapshots. Scheduler return now also captures
diagnostic state. The emulator is stopped. All twelve host executables, sixteen
Python regressions and the runtime/original-body sanitizer probes pass.

Next is validated framebuffer conversion and a real Vita presenter for the
observed mode, with an explicit progressive-mode interpretation of the deferred
encoder settings. General flips, rendering and the title/menu remain unsupported.
The [private build procedure](halo2-host-channel-20260913.md) is unchanged.
Diagnostic VPKs embed owned game image/code and must not be distributed or
uploaded as releases.
