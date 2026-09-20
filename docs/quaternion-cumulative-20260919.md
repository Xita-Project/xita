# Cumulative private-quaternion selection

Corrected perf.41 wait symbols identify quaternion-to-matrix conversion as the
largest recorded math-lock caller. The existing private-quaternion bypass was
compiled but runtime-disabled. Its September 15 isolated test did not establish
a whole-frame gain; that result does not measure the current cumulative build.

`XV_OBJECT_QUAT_DEFAULT=1` selects the existing bypass at startup when
`XV_OBJECT_PRIVATE_QUATERNION` is unset. Explicit environment values still take
precedence. Ordinary builds retain default zero. Make rejects unsupported game,
missing worker/experiment selection, invalid values and the shared quaternion
cache combination. A configuration dependency rebuilds the worker backend when
the default changes.

No arithmetic, ownership rule, alias check or scheduling handoff changes. Only
calls with private inputs/outputs and canonical constants can bypass the guard;
shared data and nested transactions keep their existing path. This is an
explicit cumulative selection, not a claim of a new measured FPS improvement.

Validation: the production private-math test passes 40 process configurations,
600 callbacks each, under ASan/UBSan. Its enabled cases leave the environment
variable unset to exercise the real compile-default startup, while disabled
cases explicitly set zero. Worker counts, private-math and fast-lock controls,
point settings and wait policies vary. Existing context/spill, ownership,
held-guard and owner-service checks remain in place. The fixture's expected
startup value was updated to honor the compile default; its original
unset-means-off assumption correctly failed the first run.

Command:

```sh
OBJECT_QUAT_TEST_ENV_UNSET=1 \
OBJECT_JOB_TEST_FLAGS='-DXV_OBJECT_QUAT_DEFAULT=1 -fsanitize=address,undefined -fno-omit-frame-pointer -no-pie' \
python3 tools/test_object_private_math.py
```

Receipt: `../quat-default-tests.log`. The cumulative hardware trial will retain
all perf.41 selections and add `XV_OBJECT_QUAT_DEFAULT=1`, fully restart, and
use ordinary campaign gameplay/logs. No automated FPS comparison is requested.


## Deployment receipt

Perf.42 / `9a1c6bf` built successfully with all perf.41 settings retained and
`XV_OBJECT_QUAT_DEFAULT=1`. Only `game-a.self` and `boot-game.txt` changed in
the VPK. Runtime SHA-256:
`90ee312aa9e2104f4509efe3acee1942d5f7ae2b1c38d6357d16a350a451f4a9`.
The updater verified 32,208,546 bytes, restarted, and boot-confirmed slot 0.
Remote status and a dashboard capture confirm perf.42 / `9a1c6bf`.

The campaign launch/check is running. Installation is verified; gameplay
activation, performance and longer stability are not yet established for this
build. Private receipts are under `../quat-cumulative-hardware/`.
