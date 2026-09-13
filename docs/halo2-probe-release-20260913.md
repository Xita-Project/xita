# Halo 2 initial device probe and release

Native attempt 34 completes the first device constructor, presents the same
black progressive framebuffer as attempt 31, and enters an intentional release
from application return address `0x0001245A`. No menu or game geometry is visible.

The owned executable's caller at `0x00012420` creates a device through
`0x00012490`, calls `0x003F9D00` with EAX zero, and explicitly releases the device
when its reference count is one. The runtime release trace records device
`0x00404FE0`, ESI zero and caller `0x0001245A`. This distinguishes it from the
constructor error cleanup, whose release return address would be `0x003F52B0`.
The purpose of the initial create/present/release sequence is inferred to be a
startup probe; its intended release is directly established by the caller and
native trace.

The H2-only AV bridge now returns progressive field index zero for the observed
option 15 query. Option 9 blanks or restores the actual Vita display buffer and
waits for a real vblank; repeated requests are idempotent. Unsupported modes,
parameters, output pointers and host display errors still stop. These void APIs
preserve EAX and their four-argument stack cleanup. The raw 2 MB scanout diagnostic
is captured only on the first presented frame.

After the release, the original miniport teardown stops at instruction
`0x003FE512`, writing zero to `0xFD002044`. Its hardware FIFO shutdown is not yet
translated. The next task is an audited idle host-channel shutdown and lifecycle
reset, preserving guest allocation/free behavior so application startup can
continue. General interrupts, repeated game flips, draws and the main menu
remain unsupported.

Validation: all 13 host executables pass, including invalid and misaligned field
outputs, blank/unblank transitions, idempotence, host failure and ABI checks.
The runtime fixture also passes AddressSanitizer and UndefinedBehaviorSanitizer.
The native attempt's screenshot, logs, device/push snapshots and packages remain
in the private artifact directory, outside Git.

Attempt 34 SHA-256:

| Artifact | Hash |
| --- | --- |
| ELF | `8fb88aeac45e7a99f7c0eed2892d312bb6c9de3b8a18fd849668617badce85f9` |
| EBOOT | `93f0d501c7881f1e54c689cc56a36e0f6c10468f7a3a47d2ac97e805bf9015e2` |
| boot.log | `fe604826238d71985ed0e355769b83366b911784d638b55a60f0f357d679c3ef` |

The package embeds privately owned executable code and image data and must not
be uploaded as a distributable release.
