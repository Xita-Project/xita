# Sealed visibility-query placement census

`XV_VISIBILITY_PLACEMENT=1` adds a count-only pump observer. Ordinary builds omit
it. It changes no query result, notification, scene, draw order, guest state,
frame ownership, or runtime setting. It asks whether the sealed packet already
contains an RTT scene end after its final potential query writer. That boundary
could justify a later experiment; **a counted boundary is not permission to
publish a result** and does not measure GPU duration.

The census lives in `runtime/xv_visibility_placement.h`, included privately by
`xv_d3d.c`. No `main.c` or generated game changes are needed. The flag applies
only to `runtime/xv_d3d.o`, with an incremental-build stamp covering both ON/OFF
transitions. There is no remote selector or runtime override. Add the flag to
the existing validated build command; preserve its other options:

```sh
make RECOMP=1 XV_VISIBILITY_PLACEMENT=1 <existing build arguments>
```

It can coexist with `XV_GPU_PACKET_TIMING=1`: their object flags and stamps are
independent. When merging both changes at the same Makefile insertion point,
retain both complete blocks. The timing observer owns `main.o`; placement owns
`xv_d3d.o`.

## Boundaries and interpretation

On the pump, `xv_d3d_visibility_prepare` scans the sealed command metadata before
GPU visibility setup. It records all allocated/issued/unissued query slots,
repeated result slots, unique referenced query slots, and the last command with
`kind==0 && visibility!=0`. Zero-index and subsequently skipped draws remain
potential writers. It never deduplicates writers because they share a query
slot, result slot, ID, or target. Clears do not write visibility in the existing
renderer and are counted separately. Invalid kinds/tags/lengths/open queries,
missing writer generations, result slots, targets and UI order are marked
unsupported. Invalid lengths are rejected before following the arrays.

The existing RTT replay calls the observer only after its actual EndScene calls.
The first successful call whose exclusive command cursor is beyond the global
last potential writer is recorded. A later query writer in another view or target
therefore prevents premature selection. Failed BeginScene/EndScene and missing
RTT targets are tracked separately. No notification is added to those calls.

After the existing result publication and scheduler notification,
`xv_d3d_visibility_complete` aggregates the packet and, for supported successful
replays with a boundary, scans the remaining command suffix. All lists remain
owned by the original final-fence lifecycle. The observer does not read the GPU
counter buffer, history values, texture/vertex data, or guest memory. Its state
belongs exclusively to the pump; no atomics, locks or new threads are needed.

The six `[visibility-placement...]` rows appear every 60 packet completions:

- The first row partitions packets into `no-query`, `slots-no-writer`, `boundary`,
  `last-writer-final`, `unsupported`, `replay-error`, or `not-replayed`.
  Error precedes unsupported in the partition. Reason counters may overlap.
  `no-buffer` is a separate subset, not successful query work.
- `last-writer-final` means the final guest replay scene left open for `main.c`.
  `all-writers-final` is its subset with every potential writer in that scene.
  At scaled resolution the later upscale is outside this observer.
- Work totals include all bounded recorded metadata, including unsupported
  packets. Reused-result counts mean additional issued slots referencing a
  result slot already seen in this packet. They do not count query IDs directly.
- `last` gives writer-bearing packets, last writer's one-based command position
  sum/maximum, and recorded draws/indices/UI after that writer. It also includes
  unsupported packets: inspect declines before using these totals.
- `tail` includes only supported, error-free replay packets with a selected
  existing intermediate boundary. Scene, command and UI positions are sums over
  those packets. Remaining scenes include the final open guest scene, even when
  it is empty. Draw/index/UI counts and maxima describe records, not successful
  GPU draws. `empty-draw-ui-tail` is retained rather than called useful work.
- Runtime overlay/settings batches, an upscale, final `main.c` scene/flip
  failures, GPU completion times and final pixels are **not observed**. RTT
  errors and an unvisited replay path are observed. No missing main-scene error
  is silently promoted into a GPU-success claim.

Reports are pump-owned and do not share the guest report reset. Their 60-packet
windows may have different boundaries from frame/flare windows. `abandoned`
means preparation replaced still-active observer state in the same slot;
`orphan` means completion did not match a prepared active frame. Either signal
invalidates an ordinary lifecycle interpretation until investigated.

## Cost and fixed bounds

There are no allocations or per-draw observer hooks. At most 2,048 commands,
1,024 UI descriptors and 512 slots are visited at preparation; at most 2,047
remaining commands are visited at completion for an admitted boundary. Sixteen
32-bit words each track result-slot reuse and referenced query slots on the
preparation stack. Actual scanned-record totals are reported.

There are two clocks at preparation, two at aggregation and two per periodic
report, when the monotonic clock is linked. `scan-aggregate-us` brackets the
first two operations; `previous-report-us` describes the preceding report and
is zero for the first. These elapsed values can include preemption. They exclude
constant work at each existing RTT end and are not a complete measurement of
observer-induced frame-time change. Reports have six bounded lines per window;
no hot-loop logging occurs.

The integrated ARM build has **832 bytes of static observer state**: 360 bytes
for per-slot state and 472 bytes for totals. The earlier 824-byte figure was the
net change in the object’s `.bss` section, which also reflects alignment; it was
not the sum of the observer symbols. Preparation uses a 224-byte stack frame
versus 40 OFF, completion 280 versus 96 OFF; RTT replay remains 136 bytes, with
a 20-byte `vp_end` callee. These compiler stack figures exclude callees and are
not whole-thread stack requirements. The host ABI also reports 832 bytes of
observer state. Ordinary ARM `.text` is byte-identical to the `e3c924b` baseline
under the checked production flags.

## Validation and physical follow-up

`python3 tools/test_visibility_placement.py` compiles the actual production
observer and replay functions in both modes under ASan/UBSan. An independent
ordered-event oracle checks 500 deterministic target/view/UI cases plus explicit
later/repeated query writers, reused result IDs, zero-index queries, trailing
UI, empty final scenes, clears, no/unissued slots, Begin/End/target failures,
frame/serial wrap, malformed metadata and full storage bounds. OFF/ON GXM/replay
traces match; metadata is byte-identical before/after observation. A real
60-packet formatter run verifies cadence and aggregate counts. This fixture
models command ordering, not GPU rendering or pixel equivalence.

`python3 tools/test_render_targets.py` retains the original RTT lifecycle tests
in ordinary mode, including queueing, depth retention, failure cleanup and
resource limits. ARM ON/OFF compile and incremental OFF/OFF/ON/ON/OFF stamp
checks pass. No device or emulator was used for this census.

For a diagnostic package, combine it with the independent packet timing flag
and retain native resolution, standard graphics, existing worker policies and
the exact camera. Let several ordinary windows complete, then collect the log
outside any measured benchmark:

```sh
python3 tools/vita_remote.py --config /private/pairing/remote-client.json \
  log /private/visibility-placement.log
rg '\[visibility-placement|\[gpu-packet|\[flare-defer|\[frame-retire|frame time:' \
  /private/visibility-placement.log
```

First establish whether `boundary` exists, whether its draw/UI tail is nonempty,
and whether errors/unsupported/ownership signals are absent. A positive count
only establishes a candidate location in the CPU replay. A future early-result
experiment must still attach and validate an exact **fragment** completion
notification, retain original final ownership and compare complete frame time.
If nearly all last writers occupy the final guest scene, the existing scene
structure offers no earlier boundary; a new split would be a different,
potentially expensive change. Command or index counts do not predict its gain.
