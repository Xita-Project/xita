# Halo 2: the constructor's first depth/stencil clear executes

Native attempt 28 completes the original 640-by-480 depth/stencil clear through
the synchronous host consumer. The real completion word advances from 5 to 7.
The next submission stops at the first software flip request. **No color frame,
displayed game output or main menu has been observed.**

## Narrow software-method translation

The owned handler at `3FF240` decodes the low five command bits as a selector and
the remaining bits as its argument. Private execution probes of the original
generated handler verified the actual reads/writes, RET-0 stack effect and
preservation of EBX/EBP/ESI/EDI for these paths:

* Selector 8, argument zero or one, writes the same argument to RDI selectors
  `E0:50` and `DF:08`. These are the DXT1-noise settings.
* Exact command 9 reads the depth/color-clear parameter registers and writes the
  latter to BAR plus the former. The submitted constructor uses only offsets
  `400094` and `400B80`, both with data zero.

The host consumer records only those typed settings. It does not expose the
arbitrary register-write protocol, trigger an IRQ or discard callbacks. Unknown
selectors, noise arguments above one, other offsets and nonzero control data
reject before state or guest-memory mutation. Parameter registers retain their
original values after command 9. The meaning of the protocol was cross-checked
against [pinned nxdk's software handler and constructor setup](https://github.com/XboxDev/nxdk/blob/29638d0b001f179b73c3513489af10ddc2986216/lib/pbkit/pbkit.c).

The new fields describe requested DXT1-noise, ZCull and ROP controls. They are not
an emulation of physical RDI, early-depth or compression hardware. Future texture
decoding and draw execution must honor them or reject the operation. Canonical
integer depth/stencil clears do not perform texture decoding or early-depth
tests. All existing format, clipping, DMA, region and host-span checks remain.

## Observed native result

The second submitted buffer completes at PUT = GET `03C2BAB8`. The consumer
reports one completed clear and 307,200 written depth/stencil pixels. The clear
uses logical Z24S8 storage at physical `038A4000`, pitch 2,560, value `FFFFFF00`,
and flags 3. Two real semaphore releases have now written guest physical
`03CAB000`, most recently value 7. Seven software updates were accepted.

The next buffer sets PUT `03C2BB08` and rejects at source/GET `03C2BAC4`, method
`0100`, value `73A00101`. This is selector 1 of the original software handler;
inspection identifies a queued framebuffer/flip path. Pending methods `012C`
and `0130` also require actual flip semantics. None has been executed. The stop
is the original KickOff PUT write at `3FAC58`, address `FD800040`.

| Attempt 28 artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ELF | 65,343,824 | `a85521d8a2e946708ede54143a03f286622c2c84f145d69c1b770553893bcbd0` |
| EBOOT | 80,076,942 | `ea555a21f7fdada782fc76edb438e00415dad8c2970fc6d9417554444f872260` |
| VPK | 22,797,779 | `ec271179fca93173af3c4d34b558d9f8554699d854e3a0edaca98bea5dfd2d24` |
| Emulator trace | 303,178 | `e6d7b7afa3476146fb0b8d74323d60c1ba81bb9cb7fb70ae79cb3e8a857edce9` |
| Boot log | 195,106 | `86d94b70966fedca065266a1584514e8794d68faba9ff96dab9dcf24c8cf6771` |
| Device snapshot | 9,376 | `ace432712b6ab89ccc2d17110b336da9aab86906c0bfa8e640c6772b1a423cd6` |
| Push snapshot | 524,304 | `2d2110e975191bd8cc831d4c51e0225dd4052528e64adbacb118b98f99e3eb52` |

All twelve host executables, sixteen Python regressions, command-state ASan/UBSan
and the private original-handler normal/sanitizer probes pass. Native compilation
is warning-free. Assets, generated code and artifacts remain private under
`private/native-28-artifacts/`; the emulator is stopped. Reproduction uses the
[isolated host-channel target](halo2-host-channel-20260913.md). The diagnostic VPK
embeds owned game image/code and must not be distributed or uploaded as a release.

Next is the framebuffer/flip contract and a real Vita presentation boundary,
including mapped input validation, display mode and the deferred encoder
settings. A successful depth clear is not evidence that presentation or rendering
works.
