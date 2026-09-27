# Collision polygon writer candidate

Status: output-page-cache experiment rejected for promotion. Perf269 is unchanged.
The packet-construction branch is still a target; no hardware FPS gain is claimed.

The preceding collection capture (vehicle-update-attribution-20260926.md) puts
polygon/capsule packet construction outside the existing native geometry query.
A first 85020 candidate attempted to reuse the output record's page translation
at 22 integer memory accesses. It retains every original store, float operation,
flag update and loop edge. Its single local page cache is invalidated after each
of four emitted preemption sites. Checked-address builds retain X_G per access.
There is no persistent collision-result cache or new concurrency policy.

`tools/prepare_collision_polygon_cache.py` prepares only a private candidate from
the exact pinned function body, rejecting source drift or unexpected site counts.
It does not install a hook or change game defaults. All generated game sources,
binaries and captured artifacts remain in `../collision-polygon-candidate/`.

## Correctness evidence

`tools/test_collision_polygon.py` and `tools/tests/collision_polygon.c` compare
full guest context, a 4 MiB arena, the page table, and full-context observations
at preemption. No collision callees are mocked: this leaf has no guest calls.
The 1,024 synthetic cases cover TOP 0–7, finite/tied/NaN/infinite normals,
empty and full packet counts, counts around the 256 cap (including negative),
0–9 input vertices, projection directions, plane/point aliases with the output,
nonidentity mappings, point-page crossings, and page remapping at actual yield
callbacks. They do not exhaust arbitrary stack aliases, active floating-point
trap modes, concurrent production scheduling or all retail gameplay inputs.

Host ASan/UBSan and Cortex-A9 Thumb ARM tests on Pi core 0 both passed 1,024
cases. A deliberately stale cache (invalidation removed) fails case 8 with an
arena mismatch after seven preemptions. The mutant confirms remapping coverage
can reject the specific bug; it does not prove universal correctness.

## Efficiency evidence and decision

ARM function sizes in the linked test: reference 0x1718 bytes, candidate 0x185e.
A separate private, uninstrumented eight-point polygon micro-workload, compiled
for Cortex-A9 Thumb and run on Pi core 0, made 300,000 calls per batch:

| Batch | Reference ns/call | Candidate ns/call |
| --- | ---: | ---: |
| 1 | 947.940 | 1002.335 |
| 2 | 946.720 | 1004.101 |

These times include identical context/count reset overhead. They measure one
synthetic shape on the Pi, not actual Vita cycles or frame-time impact. The
candidate is larger and about 6% slower in both batches, so there is no evidence
to promote it or spend a hardware deployment on it. No existing optimization
was removed; this candidate never entered the gameplay build.

The next replacement should remove translated register/x87 bookkeeping or
repeated packet work, rather than add per-access page-cache branches. Reuse the
full-state differential fixture, extend admission/alias cases for the chosen
replacement, then use representative ARM execution before hardware qualification.
