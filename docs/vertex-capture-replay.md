# Captured vertex preparation on the Pi

`tools/replay_vertex_capture.py` builds an offline replay using the production
vertex-capture FIFO, uploader and copy worker with the existing pthread Vita
service shim. Supply private `mesh_*.bin` files from `XV_MESH_DUMP`; keep them
outside the repository.

```sh
python3 tools/replay_vertex_capture.py --sanitize /private/mesh-captures
python3 tools/replay_vertex_capture.py --cc /path/to/arm-linux-gcc \
  --cflags=-static --build-only --output /private/replay-arm
```

Copy the binary and captures to the Pi, then run each mode on the reserved cores:

```sh
taskset -c 0,1 ./replay-arm full mesh_*.bin
taskset -c 0,1 ./replay-arm sparse mesh_*.bin
taskset -c 0,1 ./replay-arm packed mesh_*.bin
```

The Python launcher orders captures numerically. For direct binary invocation,
pass an explicitly ordered list if inter-draw order matters. Each invocation
checks callback completion and uploaded bytes. Full mode checks every vertex;
sparse mode checks every referenced vertex; packed mode checks the retained
16-byte prefix of 32-byte vertices and preserves 16-byte inputs. The caller's
source is overwritten after capture returns to exercise snapshot ownership.
The fixture enables partial queue waiting by default (`--partial 0` selects the
original policy); dedicated concurrency tests provide
the forced-full-queue interleavings.

The file reader checks magic, lengths, supported stride, vertex/index bounds and
trailing data. Supported input strides are 16 and 32 bytes. Captures contain
shader constants, but this first replay does not execute their shader math.
Up to 64-draw batches are fixture boundaries, not reconstructed frames. Original
resource identities, textures, all streams, native material preparation, draw
state and GPU execution are outside its scope. It is not a rendering emulator
or a complete frame replay, and it reports no FPS estimate.

Initial private archive: 492 meshes from an older lighting investigation.
Host ASan/UBSan and ARM Pi execution passed full, sparse and packed modes with
zero mismatches: 1,476 preparations, checking 20,097,280 / 9,952,352 / 10,048,768
bytes respectively (sparse totals include repeated indices). This is a useful
regression corpus, not evidence that it represents current a30 performance.
The Pi build uses the existing GCC 13 NEON compatibility header. Current a30
captures and preparation-cost measurements remain future work.

Completion batching is selectable with `--wait-batch 1`, `8`, or `16`
(the runtime accepts 1–16). Direct invocation uses
`XV_CAPTURE_WAIT_BATCH=8 XV_CAPTURE_PARTIAL_WAIT=1 taskset -c 0,1 ...`.
All three batch sizes passed the 492-capture corpus in all three modes on the
Pi: 4,428 preparations, zero mismatches. The host concurrency suite also passed
all 24 ASan/UBSan configurations, including forced queue saturation and exact
completion-frontier checks at 1, 8 and 16 jobs. These establish correctness for
the tested cases; they do not establish a performance benefit.

For preparation-only timing, set `XV_REPLAY_TIMING=1`. The fixture preloads
each batch, times capture submission through final join with CLOCK_MONOTONIC,
and validates afterwards. Timing excludes file IO, allocation of input meshes,
result checks and deliberate source poisoning; it includes first worker startup.
Normal correctness mode still poisons sources immediately after submission.
Queue-wait and full-drain counts are printed for workload interpretation.

Seven alternating-order Pi trials per mode/batch size showed no meaningful
batching gain. Median milliseconds for the entire 492-mesh corpus:

| Mode | Batch 1 | Batch 8 | Batch 16 |
| --- | ---: | ---: | ---: |
| Full | 58.299 | 58.634 | 58.562 |
| Sparse | 58.579 | 58.290 | 58.452 |
| Packed | 43.482 | 43.342 | 43.319 |

Ranges overlapped in every mode. A separate lexically ordered full-mode
pressure check recorded only two partial waits and eight full drains; this
archive is weak evidence about a saturated runtime queue. Do not extrapolate
these times to Vita frames or treat them as disproving batching under pressure.
