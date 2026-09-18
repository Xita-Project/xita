# Halo 2 PFIFO tables and instance aperture

Native attempt 14 initializes both PFIFO table registers and writes all 5,120
words of the claimed 20 KiB instance region. It then reaches the VGA CRTC index
register. This is constructor/memory initialization, not rendering: no graphics
device has returned and no frame or menu has appeared.

RAMHT stores its base, size and search fields after validating that the entire
hash table lies in the claimed instance window. RAMFC validates both encoded
base addresses before storing the configuration. Channel-context extents and
context switching are not implemented by these register latches. Disabling
PFIFO resets these fields and gates its register range. The field layouts are
from the pinned
[xemu register definitions](https://github.com/xemu-project/xemu/blob/75650bd8cd91945f7b79774e2cee0b200ca373ff/hw/xbox/nv2a/nv2a_regs.h).
The observed `0x03000100`/`0x00890110` setup agrees with the independent hardware
initialization sequence in
[nxdk pbkit](https://github.com/XboxDev/nxdk/blob/master/lib/pbkit/pbkit.c).

The PRAMIN bus maps only offsets `0x10000..0x10000+claimed_bytes` into the existing
reserved physical RAM; it does not allocate a disconnected scratch copy. Within
that window it reverses 64-byte groups while preserving bytes inside each group.
The mapping is inferred from the documented
[Kelvin RAMIN reversal unit](https://envytools.readthedocs.io/en/latest/hw/gpu.html)
and the
[RAMIN aperture's direction](https://envytools.readthedocs.io/en/latest/hw/bus/bars.html).
NV2A-specific physical aliasing has not been measured on hardware here. The
current xemu implementation itself uses separate RAMIN storage; this model's
physical alias choice must remain explicit until independently measured.

Tests establish a one-to-one mapping of all 5,120 claimed words, check both sides
of 64-byte boundaries and the first/last bytes, and reject accesses outside the
claim. Generated synthetic x86 verifies real bus writes are visible through the
physical alias and vice versa. PCI memory disable and PFB disable prevent access.
The PFB instance write-protection register exposes the current disabled state;
enabling it is rejected because ordinary CPU aliases are currently writable.

Local cache/register/timer/instance/aperture tests, 14 Python regression tests,
and the aperture ASan/UBSan run pass. The incremental four-job Vita build passes.
This milestone changes only Halo 2 files and its synthetic bus test.

Native attempt 14 records:

| Operation | Observed value |
| --- | --- |
| `0x401E05`: RAMHT write `0xFD002210` | `0x03000100` |
| `0x401E49`: RAMFC write `0xFD002214` | `0x00890110` |
| `0x401E5B` / `0x401E60`: PFB protection read/write | `0` / `0` |
| Instance writes from the original loop | 5,120 writes, 5,120 unique words |
| Active instance claim | `0x5000` bytes |
| Next checked access | CRTC index `0xFD6013D4`, function `0x401E96` |

The final byte write still uses the checked pointer path; its instruction is
identified statically as `0x401E98`, while the runtime guard reports the function
and address. No value was supplied or stored at that boundary. The next bounded
step is byte bus translation plus indexed CRTC lock/configuration semantics,
followed by another native trace. Actual FIFO submission, command processing,
GPU interrupt delivery and a rendering backend remain ahead.

All owned assets, generated C, packages and full traces remain private. The
VPK embeds owned game code/image data and must not be uploaded as a distributable
release.

SHA-256 from `private/native-milestone-14.json`:

| Artifact | SHA-256 |
| --- | --- |
| ELF | `04f778ed1bd38d05145d8840fbafb55fb90bee4495a26b606528d8f663d8a3c5` |
| EBOOT | `f2788612dad068b414aa06ec30198143b9dfb3e60ed38f1fcf4f138586e3a85d` |
| VPK | `d19acf1ba7d8678ba189e747ed8e24c2270c43d726f8e67db86759df389ad679` |
| Native trace | `df377dcc6287cf1c86038f4b5e121ecb82c254df16b14b00cd109c3907db9955` |
