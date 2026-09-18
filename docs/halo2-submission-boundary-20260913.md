# Halo 2 device and submission boundary

The recommended rendering path is a Halo 2 host channel that consumes the
original XDK's command stream. Retaining every hardware initializer would require
much more of an NV2A emulator. Replacing only `CreateDevice` with the CE HLE would
leave incompatible calling conventions, object layout and inline command writes.
The current native boot remains at attempt 15's strict PVIDEO stop: no successful
device creation or rendered frame is claimed here.

## Owned-code contracts

These observations are for the executable pinned by `halo2_5849/profile.json`.
Public symbol matches supply names; owned caller/callee analysis establishes the
interfaces below. Addresses beyond the native stop are static observations,
except for the separately identified host unit probe.

| Boundary | Established contract | Remaining requirement |
| --- | --- | --- |
| `0x3F5240` CreateDevice | EAX flags, ECX output slot, one stack presentation pointer, RET 4; static device `0x404FE0`, library slot `0x407488` | Preserve original guest object and ABI |
| `0x3FBC10` constructor | Two stack arguments, RET 8; native reaches its miniport call at `0x3FBC8F` | Separate software initialization from hardware objects/channel setup |
| `0x3FA5E0` push allocation | ESI device; sets cursor/limit at +0/+4, allocation base/end at +0x24/+0x28; allocator failures return HRESULT | Preserve allocation ownership and ring-space semantics |
| `0x3FE005` miniport init | EAX miniport (device +0x1C28), RET; initializes DPC/interrupt/list/gamma state and GPU hardware | Host miniport contract must account for all consumed fields and callbacks |
| `0x3FE374` DMA object creation | EAX miniport, ESI output descriptor, four stack arguments, RET 0x10; writes instance descriptor and a 16-byte guest descriptor | Translate validated DMA address/class/limit objects, not unconditional success |
| `0x3FAC30` KickOff | EDX device, RET; normal cursor +0, recording cursor +0x770; submits cursor low 28 bits through `[device+0x1C20]+0x40` | Consume bounded commands before updating channel progress |
| `0x3FADE0` write-combined flush | SFENCE followed by PFB flush request/poll, RET | Explicit host publication barrier if replaced |
| `0x3F9D00` Swap | EAX flags, RET; manipulates swap count at device +0x2478 and emits/awaits work | Framebuffer ownership, fence completion, presentation and callbacks |

The channel control +0x40/+0x44 offsets agree with the documented
[DMA PUT/GET interface](https://envytools.readthedocs.io/en/latest/hw/fifo/dma-pusher.html).
The original constructor calls KickOff at `0x3FC343`, then polls GET at
`0x3FC34E` and `0x3FC37F`. Later it polls PFIFO cache positions and PGRAPH status
before continuing. Updating GET alone would not prove rendering or GPU completion.

The miniport hardware leaf `0x3FE1D3` is not a sufficient replacement by itself.
After it returns, the outer miniport initializer enables interrupts and engines.
The device constructor also writes RAMHT/instance objects directly, calls the
Kelvin context initializer, and waits for its initial command batch before
framebuffer initialization. Those operations must have explicit host equivalents.

The public
[XDK 5849 signatures](https://github.com/Cxbx-Reloaded/XbSymbolDatabase/blob/20eced544726f5558c5a408458f38a086cc4e543/src/OOVPADatabase/D3D8LTCG/5849.inl)
are candidate-name evidence, not authority for every ABI. In particular, the
scanner's stack-this description for miniport init disagrees with the verified
EAX caller. No bulk symbol binding is justified.

## Bounded parser prerequisite

`games/halo2_5849/push_stream.c` implements a single-allocation command parser with
increasing/non-increasing packets and both jump encodings. It preserves a packet
split across PUT updates, validates ring bounds, rejects implicit wrap, limits
work by a word budget, and leaves a rejected method's data word pending. A
consumer must accept a method before parser progress advances. CALL/RETURN,
unknown command forms, DMA object translation, hardware caches, interrupt
signaling and graphics methods are outside this component.

Packet encoding and channel roles were checked against the primary documentation
above and the pinned
[Cxbx command-stream implementation](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/hle/D3D8/XbPushBuffer.cpp).
The component parses methods into callbacks; it does not impersonate a completed
GPU operation. It is not linked into the normal native boot yet. There are no
new success-return device stubs or accepted unknown registers.

Synthetic tests cover parameter words resembling control-flow commands,
subchannels, zero-count packets, method-index wrap, split packets, rejection
without replay, both jumps, ring-end faults, invalid state, and cyclic input
budgets. All six Halo 2 host tests and parser ASan/UBSan pass.

A private ASan/UBSan host probe executes the existing generated original
`0x3F52F0` EdgeAntiAlias function with an explicitly initialized test device/ring.
It verifies RET 4 stack behavior, ESI preservation, cursor advancement by 12 and
the guest cached state, then decodes the two emitted methods `0x320` and `0x324`
on subchannel 0 with value 1. Its recording consumer does not execute graphics.
The probe is a unit test of an owned function, not a normal boot trace or native
Vita rendering evidence. Its generated function and harness remain private.

## Next bounded rendering task

Implement the host-channel/device contract before removing any constructor
barrier: inventory the miniport fields consumed by the constructor, map its DMA
objects and RAMHT bindings, connect KickOff to a strict method consumer, and
preserve separate submitted/consumed/completed states. First target should be an
original guest clear on a validated framebuffer, then original swap/presentation;
unknown drawing/shader/state methods must still stop. Native evidence must show
the original request producing the correct framebuffer pixels, not merely a
successful parser return or a diagnostic-colored screen.

Shared `recomp/kernel/xd3d.c` currently has no-op MakeSpace/KickOff bindings and CE
state/callback assumptions. `runtime/xv_d3d.c` exposes useful native draw/clear
recording primitives, but needs a Halo 2 assembly with its own shader and frame
integration. Neither shared file is changed by this milestone.

Owned bytes, generated C and diagnostic packages remain private. A VPK embedding
owned game code/image data must not be uploaded as a distributable release.

## Reproducible boundary fingerprints

Each range below is half-open and ends after the checked return instruction;
these hashes identify the owned byte ranges without committing their contents.

| Range | SHA-256 |
| --- | --- |
| CreateDevice `0x3F5240..0x3F52E8` | `c664a89d2c13bfeb171af813845bcf56d38de4a68c18860e010a8f749c29b4bf` |
| Constructor `0x3FBC10..0x3FC528` | `42e31d049286dcb30646be5199b75e760d1d47a3869c3f3db8e08a7a1b0a3206` |
| Push allocation `0x3FA5E0..0x3FA676` | `db8daabeda3e920dd9b91dca2f5a8305d772694aaf11cb62d1e491530fef2847` |
| Miniport init `0x3FE005..0x3FE165` | `3c7fccae26a9a87e47c60a69737aa6cde7340fee827b8e4b0338e96236947b4e` |
| Hardware leaf `0x3FE1D3..0x3FE254` | `3d04a1ba33d6aaa1315f80bc9882d4d1956d9480985da864186b247d883adceb` |
| DMA object `0x3FE374..0x3FE43D` | `81fc6945b604b2bcac0ce61372d0a774f01104cd34b1380406070ef44d5b48af` |
| KickOff `0x3FAC30..0x3FACD2` | `e1ed05343df2b0078d7d9eb5574054e98c5cd9c0021ebc7c227ba7f26b01a051` |
| Flush `0x3FADE0..0x3FAE0D` | `97d24aa2909c3ed0a59eb665c9759882cc01544230ea133ac77d1c1c532afc81` |
| Swap `0x3F9D00..0x3F9DEB` | `c3d568b465fcd3b1963e63d5a3bcb686f21120e847d41cb1b03794c8cbaca638` |
| EdgeAntiAlias `0x3F52F0..0x3F532C` | `06398f6130e6c73df56dc73c3eca245fe783d77ade9d926084b683ce8f4c6fff` |
