# Halo 2 decoded channel capture

The private startup harness now writes `channel-at-stop.json` alongside its
existing device, push-ring and scanout captures. It runs only at a stopped
startup/graphics boundary, when a virtual host channel exists. It does not
execute a draw, advance DMA GET, retire a semaphore, or change guest memory.

The snapshot records the parser position, attachment setup, objects and bindings,
tracked setup words with their validity bitmap, all 192 constant vectors and
136 program slots, and software-method state. The rejected method and value
remain in the ordinary fault log. Raw program/constant words are uint32 values,
preserving NaNs and signed zero. Unwritten slots remain zero; there is no implied
validity or active-shader coverage for those slots. This is tracked host state,
not a complete NV2A register dump or a working draw backend.

Field-by-field serialization excludes host pointers, C padding and guest memory
contents beyond already tracked state. The file can still contain original
shader words and constants, so keep it in the private emulator lab. Do not add
captures to the source repository, website or releases. A snapshot is complete
only when its completion log is true and its JSON parses.

Host validation covers exact bit patterns at the first/last constant and program
slots, setup bitmap limits, object/binding fields, 64-bit counters, unchanged
channel state and stream-write failure under ASan/UBSan. All 15 existing Halo 2
host tests pass. VitaSDK compiles the serializer and boot integration. Native48 integrates the snapshot and logs completion=1. Its 9,600-byte JSON
parses with SHA-256
`f461fd24cc4d5a7292da55faa2f857f8df7d93b89f86f2f97921eabe54f70327`.
GET and PUT both equal `03B54280`, with no pending method. The second channel's
program bank, surface format and pitch are still zero; no second-device draw
setup has been submitted. The capture remains private at
`native-48-artifacts/channel-at-stop.json`. This validates serialization of
actual tracked state, not a menu or any new rendered output.
