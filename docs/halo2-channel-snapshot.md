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
host tests pass. VitaSDK compiles the serializer and boot integration. Native
integration will be verified in the next startup build; this change does not
claim a menu or any new rendered output.
