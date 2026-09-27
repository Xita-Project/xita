# Visibility-mask register-local experiment — 2026-09-27

Rejected for deployment: correctness passed bounded fixtures, performance did not.
No production hook, runtime policy, or installed executable changed. Perf275
remains installed provisionally, with perf273 available for rollback.

## Avoid repeating completed work

The older surface-scan prototype is superseded by the qualified
[scene-index run implementation](native-scene-index-run.md). The current 54010
body includes its three hooks and XV_SCENE_INDEX_RUN=2 is in the hardware launch
configuration. Its earlier actual run-length census is already documented.
An attempted preparation of another census was stopped by a retained-body
identity assertion before compilation: the newer body contains those hooks and
callback timing scopes. Do not use the older point-location shard as proof of
current whole-function identity. No duplicate census was run.

## Distinct candidate: 53E90

The separate visibility-mask routine remains translated. Prior zero-run skipping
regressed dense inputs. This experiment instead retains its original branches,
loads/stores and all scheduler handoffs, keeping eight integer registers local
and publishing/reloading them at handoffs and exits. Flags remain in the context;
only reviewed flag-reading/updating helpers are allowed. The generator pins the
retained-body hash and writes proprietary generated code only to a private output.

Host ASan/UBSan and Cortex-A9 Thumb ARM on Pi CPU0 both passed 768 complete
context/memory cases and 2,588 matching yield-visible contexts. Cases include
mask density, budgets, page relocation, bound mutation, and a handoff changing
index/source registers. Existing fixture limits apply: not exhaustive physical
aliases, context/arena overlap, checked-memory callbacks or live-game equivalence.
Private ../surface-mask-register-candidate/ retains generated bodies, fixture,
binaries and host.log/pi.log. No jobs remain running.

| Mask | Original ns/call | Candidate ns/call |
|---|---:|---:|
| empty | 274.7 | 253.8 |
| bit 0 | 5,566.9 | 10,027.5 |
| bit 31 | 5,459.7 | 10,130.3 |
| all set | 12,321.9 | 11,196.5 |
| alternating | 8,684.4 | 11,586.4 |
| bits 0 and 16 | 5,884.9 | 10,097.6 |

50,000 calls per mask/mode, 128 entries, common setup restored per call. This is
one synthetic Pi experiment, not Vita timing. ARM body grows from 0x772 to 0x8cc
bytes. Sparse and mixed regressions outweigh the small empty/dense improvements;
do not deploy or re-run this GPR-only formulation without a new mechanism.

The current hardware log's outdoor scene helper uses approximately 44 ms CPU
per frame, while deferred-recording drains are about 0.5–1 ms there. These are
separate observations, not additive frame costs or firing-specific attribution.
The next implementation needs to reduce actual preparation work or its translated
flag/memory overhead, rather than merely moving registers into C locals or
retuning already-small queue waits.
