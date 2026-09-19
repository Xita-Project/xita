# Spatial-query retirement hooks and remaining worker boundary

The current perf.30 holder capture confirms that `56670` remains a substantial
outer lock holder. It combines a numerical query prefix (`56670..566DE`) with
a datum allocation and linked-list publication tail. The existing typed query
prototype replaces only that prefix. Releasing the entire original function's
lock would expose shared list/allocator state; do not do that.

Added explicit typed-query invalidation at the audited primary entries of
`58440` (map reset) and `58CD0` (BSP switch), before any original instruction or
retirement callback. Previously the prototype had per-batch teardown and owner
service invalidation but no explicit hooks at these two retirement entries.
The hooks only invalidate the batch's atomic validity flag. They neither free
an in-flight snapshot nor construct a new one; existing drained batch teardown
retains responsibility for storage retirement. Ordinary builds compile the hooks
out, and perf.30 remains unchanged on hardware.

The complete audited-image check remains required by `HaloHooks`; each entry
also checks its own 16-byte signature. The owned-image hook test passed both
entries, all 32 single-byte prefix mutations, unrelated entry rejection, and
unsupported-image suppression. It verifies insertion before other entry hooks.
This is not a live map-transition or concurrent-worker proof.

Still required before enabling the typed adapter:

- Account for indirect/overlapping entry paths and writes to consumed geometry
  within a worker batch, not merely changes to root pointers.
- Select useful queries without penalizing the common tiny traversal cases.
- Measure snapshot construction plus capture, numerical work, replay, validation
  and list publication together. Earlier synthetic gains excluded real kernel
  and allocator latency; some complete small queries regressed.
- Only then shorten a depth-one query transaction using immutable geometry,
  private query/result state and validation before publication. Keep nested
  collision transactions intact until their actor writes are isolated too.

The earlier frame-snapshot buffer prototype is still not a complete game-state
snapshot. These retirement hooks advance the existing spatial-query integration;
they do not enable stale-frame AI or remove render/simulation joins.

Reproduce the owned-image boundary check with:

```sh
python tools/test_cluster_lifetime_hooks.py --xbe /path/to/default.xbe \
    --manifest /path/to/game_manifest.json
```
