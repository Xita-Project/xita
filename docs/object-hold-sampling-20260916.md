# Sampling the object-worker lock holder

The existing lock-site reports identify the caller waiting for the shared
guard. They cannot identify which operation retains it. The latest campaign
capture again puts quaternion and matrix helpers among the largest waiters;
that alone does not justify changing their arithmetic or ownership rules.

`XV_OBJECT_HOLD_PROFILE=1` adds a research-only diagnostic to the object pool.
Ordinary builds compile out its acquisition/release instrumentation. In a
diagnostic build, remote comparison **42**, `object-holds`, enables sampling
only in the middle Off/On/Off arm. It requires initialized active workers and
the fast recursive guard policy. Selection and exact initial-mode restoration
run at the existing drained frame boundary. It defaults Off and has no
environment variable that automatically enables it.

Each worker independently samples approximately one in 64 outer acquisitions
with a deterministic pseudorandom sequence. Timing starts after acquisition
and ends before mutex release. Nested guards do not start another sample;
early release for proven private math ends the original sample. No guest
addresses or stack contents are inspected. Each lane has a bounded 32-site
table plus an overflow bucket, updated after releasing the mutex. Reports and
resets run only after the pool has joined. The sampling sequence continues
across report windows to avoid repeating the same short prefix.

`[object-holds]` records observed outer calls and completed samples per lane.
`[object-hold-site]` attributes those samples to the outer native return PC.
Resolve physical PCs against the exact installed ELF using the existing
`xv_object_math_lock` code anchor; a relocated Vita address is not an ELF
address. Verify that each normalized return PC follows a call to that anchor.

## Interpretation

- Sampled elapsed time includes preemption and any owner-service pause while
  the worker retains its guard. It excludes the preceding acquisition wait.
- Owner-held scopes and the cost of OS unlock itself are outside this trace.
- Sample sums are not total hold time, exclusive CPU time, or a frame-time
  saving. Maximums describe sampled calls only.
- Sampling and reporting add overhead. The comparison measures that overhead
  while preserving worker count, lock behavior, graphics, and draw order.
- Rare long holds can be missed. Use repeated comparable captures before
  selecting a critical section to restructure.

## Validation

Production pool tests cover two, one, and zero workers, both contention-wait
policies, nested scopes, owner audio/cache handoffs, and attempted reporting
before scope retirement. Observed outer calls reconcile with actual worker
acquisitions; site counts reconcile with completed samples and clear on the
next report. Normal, ASan/UBSan and TSan runs pass.

The private-math fixtures also run with sampling enabled across 40
configurations under each sanitizer. They exercise early unlock, nested and
shared-output rejection, full context/memory comparisons, held-guard
rendezvous, and owner-service quiescence. Controller tests cover initial Off
and On, completion, cancellation, lost gameplay control, and absent or
unavailable implementations. The actual HTTP service/client and production
frame-boundary tests cover selector 42 without renumbering prior selectors.

```sh
python3 tools/test_object_holds.py
OBJECT_HOLD_TEST=1 OBJECT_JOB_TEST_FLAGS='-DXV_OBJECT_HOLD_PROFILE -fsanitize=thread -fno-omit-frame-pointer -no-pie' \
  python3 tools/test_object_private_math.py
python3 tools/test_frame_acquisition.py
python3 tools/test_remote.py
```

## Hardware status

The candidate runtime SHA-256 is
`ed13f122d76d7b3a14bb5e4ff7739609cba145e1a5615d864c3146f2de3af9fd`.
Package verification finds only `game-a.self` and `boot-game.txt` changed;
all generated guest objects match the preceding build. Only the object pool,
main runtime, benchmark controller and remote handler objects change.

The candidate is installed and boot-confirmed in slot 0. Its startup log
confirms standard graphics on the physical Vita: native resolution,
256-pixel textures, game filtering and automatic mip smoothing, original
material/model/glow/particle detail and decal limits, all four visual switches
On, additional texture compression Off, and no frame cap. Existing triple
buffering, indexed checks and parallel uploads remain enabled. Effective
clocks are CPU 444, GPU/bus 222 and crossbar 166 MHz. The physical capture is pending; this
diagnostic is not an FPS improvement or proof of the stable 20 FPS goal.

Private build, sanitizer, settings and device receipts are under
`direct-cluster-query/object-hold-profile/` in the September 14–16 validation
directory.
