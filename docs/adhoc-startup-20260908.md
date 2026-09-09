# Ad hoc tester startup investigation — September 8, 2026

The first collected hardware log contains two launches of the original
`XITAADH01` diagnostic. Both returned **0x805B0017** from `sceGxmInitialize`
and exited before networking initialized. No peer discovery, packet-loss, RTT
or stream-transfer result was produced. This is a tester startup failure;
it provides no evidence for or against Vita-to-Vita connectivity.

The installed executable matched the original package. The log and executable
were archived over read-only USB before preparing the replacement.

## Replacement: 20260908b / version 01.01

- Use the SDK-default 16 MiB GXM parameter buffer, a display callback and a
  one-entry display queue, following the configuration used by Xita and the
  [VitaSDK dialog sample](https://github.com/vitasdk/samples/blob/master/ime/src/main.c).
  The previous 256 KiB size is the SDK's documented minimum, so the driver error
  alone does **not** establish that the size was invalid. Hardware must confirm
  whether this configuration resolves initialization.
- Alternate two mapped CDRAM framebuffers with separate color surfaces and sync
  objects. CPU text painting waits for the preceding dialog/display submission
  and display switch to retire before reusing that buffer. This is a 10 Hz
  connectivity diagnostic; its display waits contribute to measured RTT.
- Preserve a CPU-only error page if initialization fails after allocating the
  first framebuffer. It reports that networking never started and waits for Cross.
- Stop on submission/display errors. At shutdown, verify display detachment
  before freeing storage. If retirement cannot be confirmed, leave graphics
  resources to process teardown. The log identifies revision `20260908b`.

The main Halo executable, its frame queues and graphics settings are unaffected.

## Validation and an emulator teardown issue

The VitaSDK build passes with `-Wall -Wextra -Werror`. Run
`python tools/test_adhoc_screen.py` for 24 ASan/UBSan cases against the actual
screen implementation: deferred display callbacks, alternating buffer ownership,
all initialization failure points, submission/callback failures, and shutdown
failures including a display implementation that ignores detachment. Host calls
are modeled; the ARM memory barrier is replaced with a host fence.

The initial emulator check reached network setup and rendered the call log.
Its ad hoc calls are stubbed or unimplemented, so the returned group failed our
`XITA` check. Exiting then crashed the emulator. A separate UI-only executable
reproduced the crash without initializing networking: it happened while freeing
the last displayed framebuffer after a successful detach call.

[Vita3K's display implementation](https://github.com/Vita3K/Vita3K/blob/master/vita3k/modules/SceDisplay/SceDisplay.cpp)
returns success for a null framebuffer without clearing the displayed address.
This matches the observed failure. The replacement checks the current framebuffer
after requesting detachment and waiting for vblank; it preserves owned storage
when the display still references it instead of relying on the return code alone.

The final package was retested in the private Vita3K instance: GXM initialization
succeeded, the network setup/failure screen remained visible, and Cross returned
to the emulator library without a crash. The log confirms that framebuffer
readback triggered the safe process-teardown path. The emulator's incomplete
ad hoc implementation still prevents a connectivity result.

The final package still needs a fresh hardware run on **both** Vitas. A successful
build or emulator startup does not establish a wireless link or a working Halo
System Link match. Use the [two-Vita procedure](../tools/adhoctest/README.md),
then collect `ux0:data/xita/adhoc.log` from each console.
