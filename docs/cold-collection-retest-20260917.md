# Collection selection for a fresh gameplay launch

`XV_NATIVE_OBJECT_COLLECT_DEFAULT` selects the collection helper's startup
fallback when the **runtime** `XV_NATIVE_OBJECT_COLLECT` environment entry is
absent. It defaults to **0**, accepts only 0/1, and does not compile the helper
by itself. The existing build switch `XV_NATIVE_OBJECT_COLLECT=1` still includes
the optional helper and guest hook.

| Compiled default | Runtime environment absent | Runtime `0` | Runtime `1` | Runtime empty string |
| --- | --- | --- | --- | --- |
| 0 | OFF | OFF | ON | OFF |
| 1 | ON | OFF | ON | OFF |

Explicit environment values retain the existing `atoi(value) != 0` behavior;
an empty or nonnumeric value is not treated as an absent entry. The build switch
and Vita runtime environment share a name but are separate controls. Check the
effective startup log rather than inferring the runtime mode from build flags.

The new `[object-collect] process-start mode ...` line runs after the dashboard
returns and its final configuration handoff, before pump/guest thread creation.
It uses the existing atomic effective-mode getter to resolve configuration once.
It neither invokes an override nor clears counters. Existing periodic collection
reports remain unchanged. No new runtime benchmark, controller or remote route
is added; old comparison interfaces are unchanged and are unnecessary here.

The qualified `171F10` walk and `1716F0` rejection arithmetic, all memory and
guest-state effects, scheduler boundaries, FP handling and original fallback
are unchanged. This patch changes startup policy only. Neither cold launch nor
combined enablement establishes a performance gain or broader gameplay safety.

## Build and launch

Use the existing owned stage and retain all unrelated build flags/assets. With
both helpers compiled, the following selections keep admission hooks present
while choosing their startup modes:

```sh
# Both candidates OFF baseline.
make RECOMP=1 XV_NATIVE_OBJECT_COLLECT=1 XV_NATIVE_OBJECT_COLLECT_DEFAULT=0 \
  XV_NATIVE_COLLISION_VERTICES=1 XV_NATIVE_COLLISION_VERTICES_DEFAULT=0 xita.vpk

# Collection only.
make RECOMP=1 XV_NATIVE_OBJECT_COLLECT=1 XV_NATIVE_OBJECT_COLLECT_DEFAULT=1 \
  XV_NATIVE_COLLISION_VERTICES=1 XV_NATIVE_COLLISION_VERTICES_DEFAULT=0 xita.vpk

# Combined with the existing collision-vertex startup candidate.
make RECOMP=1 XV_NATIVE_OBJECT_COLLECT=1 XV_NATIVE_OBJECT_COLLECT_DEFAULT=1 \
  XV_NATIVE_COLLISION_VERTICES=1 XV_NATIVE_COLLISION_VERTICES_DEFAULT=1 xita.vpk
```

These are candidate-specific arguments, not a replacement for the complete
retained build command. For default-based comparisons, keep the runtime collection
environment entry absent; an explicit runtime `0` will still disable collection.
Use a complete application restart and ordinary gameplay, with other selections,
graphics and checkpoint/map held constant. Record executable hash, startup logs
and passive activity reports. No live toggle is needed. Combined results are a
separate observation; do not add synthetic or separately measured savings.

Stage only `Makefile`, `runtime/main.c` and
`recomp/kernel/xk_object_collect.c` from this change. The existing generated
collection hook is unchanged, so this patch needs no guest-unit regeneration.
The target-specific startup macro is passed only to `xk_object_collect.o`;
the original `-ffp-contract=off` rule remains. A separate startup stamp means
changing default 0/1 rebuilds the helper and its dependent archive/link, without
recompiling the generated units or main. Compile enable/disable still tracks
the hooked unit, helper, main/UI consumers and archive membership. Repository
compile and startup defaults remain OFF.

## Validation

`tools/test_object_collect_startup.py --output-dir /new/private/output` runs
12 ASan/UBSan cases using the actual helper getter and extracted production
startup-log block: omitted/explicit default 0/1 with absent/0/1/empty environment.
It verifies the log's source placement after configuration and before guest
threads, and checks cached mode resolution. Its production-Makefile fixture
uses recorded compiler/archive commands to prove exact macro scope and
incremental invalidation across eight feature/default transitions. It checks
same-mode no-op builds, no unrelated guest-unit rebuild, retained FP flags,
stale-member removal and invalid-default rejection. This fixture is not a VPK
build or SDK link test.

The actual owned-XBE differential test is run separately for defaults 0 and 1:

```sh
python tools/test_object_collect.py --xbe /owned/default.xbe \
  --manifest /owned/game_manifest.json --output-dir /new/private/default1 \
  --startup-default 1 --modes startup startup-env-0 startup-env-1 startup-env-empty \
  --cases 256 --sanitize
```

Both defaults passed all four modes: **2,048 full-context/full-memory cases**,
with 2,961 matching call/yield events per 256-case run. Enabled runs each
visit 6,230 objects and skip 5,068, including 238 frame-alias regression skips;
disabled runs admit none. A link wrapper asserts that startup cases call no
live override. The existing on/off/default/environment test modes also pass
256 cases each. The original helper children/stubs and oracle limitations are
unchanged from `claude-object-collect-arm-qualification-20260916.md`.

VitaSDK ARM compilation passes both defaults with production helper flags.
Default 0's complete `.text` is byte-identical to base `c1d2832`: 2,968 bytes,
SHA-256 `2706d61a2b9f653b573e1a88859b50cae684252c317af7bbbcccff555f2e70e6`.
Default 1 is 2,980 bytes. The qualified math/walk source tail is byte-identical
to that base. No ARM timing or FPS improvement is claimed.

Private receipts are under
`validation/engine-restructure-20260914T2300Z/direct-cluster-query/cold-collection-retest/`
in the worker-sizing backup: `startup-config-final/result.json`,
`default{0,1}/result.json`, `legacy-controls.json` and `arm-compile.json`.
No authoritative source/stage, device or emulator was modified during this work.
