# Draw-route profiling

`7A960` dispatches to `7A130`, `7A1F0`, `7A3D0`, or `7A460`. Its inclusive
cost includes those routines and their graphics calls; it is not a measure
of replaceable dispatcher overhead. Each route loops over draw inputs and
calls SetStreamSource, SetIndices, and DrawIndexedVertices.

For a private generated stage:

```sh
python3 tools/patch_scene_phase_timers.py /private/stage/recomp \
  --parents 0007A960,0007A130,0007A1F0,0007A3D0,0007A460 --hle-calls
```

Rebuild that stage and launch with `XV_SCENE_PHASES=1`. The phase report groups
each route's inclusive elapsed time with its HLE children (`183AD0`, `181B30`,
`1842D0`). The remainder includes guest preparation, instrumentation overhead,
and scheduling delays; it is **not CPU self time**. HLE timings include the
existing proxy, recording, and synchronization behavior. No calls are bypassed
or reordered. The existing profiler is per owner/helper and retains its stack
and table capacity limits; missing or truncated reports must not be interpreted
as zero cost. Other installed phase instrumentation also activates with this
flag, so these runs are diagnostic rather than clean FPS qualifications.

The patcher tests cover opt-in HLE wrapping in both guest-call modes, unchanged
instruction order, unselected functions, idempotence, and the final function in
a shard. Generated game code and private captures remain outside the repository.
