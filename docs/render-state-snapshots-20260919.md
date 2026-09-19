# Render-state snapshot implementation

Status: owned model-pose payload implemented and concurrency-tested. **Not wired
into gameplay, not deployed, and not a completed simulation/render split.**
Hardware remains perf.35. No FPS gain is claimed.

## Implemented ownership and payload

`recomp/kernel/xk_render_snapshot.c` extends the existing two-slot ownership
exchange with complete, copied model poses. Each published state has an explicit
world generation and simulation tick. Each pose contains its full salted object
handle, model identity, node count and up to 64 complete 13-float matrices.
There are no guest memory pointers in the published payload.

The consumer pins one immutable state while the producer fills the other slot.
A pinned slot cannot be overwritten; a busy producer returns without waiting.
The caller must handle that outcome explicitly, not silently advance simulation
against missing render state. Duplicate object identities, bad counts and a full
256-object capacity reject the whole attempted publication. An empty publication
represents an empty scene. Consumers reject a different world generation and
match both salted object identity and model identity, preventing a recycled
object-table index from selecting an unrelated pose.

This is caller-owned storage, with no runtime allocation or thread startup.
Two banks consume roughly 1.7 MiB; a gameplay allocation must be measured against
actual memory headroom. The caller must reset only after all readers stop.
This is a pose schema, not a claim that all render-visible dependencies fit in it.

## Validation

`tools/tests/render_snapshot.c` exercises 10,000 producer ticks with concurrent
consumer yields and full matrix-content checks. It also covers source mutation
after copying, pinned-slot backpressure, world transitions, changed object salts,
changed model identity, object removal, empty scenes, invalid inputs, duplicate
identities and capacity exhaustion. ASan/UBSan and TSan runs pass. The production
payload source compiles with VitaSDK ARM/Thumb and `-Wall -Wextra -Werror`.
These tests prove the exercised ownership rules, not engine integration or GPU
resource lifetime.

Reproduce the host test from the repository root:

```sh
cc -std=gnu11 -O2 -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined -pthread \
  tools/tests/render_snapshot.c recomp/kernel/xk_render_snapshot.c \
  recomp/kernel/xk_frame_snapshot.c -o /tmp/xita-render-snapshot-test
/tmp/xita-render-snapshot-test
```

Use `-fsanitize=thread` instead for the separate concurrency check.

## Integration boundary still to implement

The audited generated primary 5B4A0 reads the full object handle from the entry
pointed to by EDI, then follows the object table. Its descendants include cache
lookup/refresh through 5AE10 and model traversal through 5B190. A26B0 receives
pose input through ECX, loads a model resource, and writes shared render globals
before producing its stack-local palette. The palette buffer address alone is
not a safe cross-frame object identity. Copying those matrices and removing the
object-job join would leave these other accesses shared.

The next integration must:

1. Capture poses at a completed simulation boundary with the real world epoch,
   full object identities and all attachments needed by the selected draw path.
2. Pin a completed generation at render start and route the selected model-pose
   consumers through that view. Preserve live cache refresh until its inputs and
   writes have an explicit owner; do not mix old bone matrices with a new root.
3. Establish ownership for visibility, lighting, model/cache lifetimes and
   creation/removal. The current payload intentionally does not invent those
   layouts or claim to cover them.
4. Dispatch independent preparation against owned input/result buffers. Remove
   only joins whose readers have actually moved to immutable data. The existing
   command-buffer/GPU retirement path remains responsible for GPU resources.
5. Build an opt-in candidate, verify the running revision after restart, and
   compare ordinary checkpoint and crowded-campaign gameplay on physical Vita.
   Record missed publications, snapshot age, memory use, frame time and visual
   correctness. No predicted FPS increase substitutes for that test.
