# Private cumulative polygon-edge startup trial

`XV_POLYGON_EDGE_TRIAL=1` selects the already-qualified exact B77C0 helper at
the first joined gameplay-owner Present/Swap. It is a separate default-OFF
process-start selector, compatible with the current clip-region startup trial.
It changes no arithmetic, guest hook, synchronization, saved setting or worker
policy. It is prepared for an ordinary fresh-launch trial; no new device result
or FPS improvement is claimed.

The older [polygon-edge comparisons](native-polygon-edge.md#physical-comparison)
showed small improvements in one view and mixed results in another. They did
execute the helper. Those results left it excluded rather than establishing
a correctness failure or a general regression. Its modeled instruction saving
is not a hardware timing prediction, and empty work was slower in that model.

## Startup contract

The optional helper runs after the existing clip startup helper and before the
existing census scope/renderer callback in both real Present and Swap HLEs.
The production `xv_object_census_boundary(c)` first verifies the actual native
owner, exact runnable current guest context/fiber, empty joined queue, no pending
owner/service work and no enclosing tracked owner guard. It does not initialize
the object backend. Menus, an uninitialized/disabled backend or an inadmissible
caller defer selection. Bootstrap and network threads cannot bind the controller.

An already initialized polygon controller is preserved without admission probes,
counter drains, overrides or reinitialization, including when a region remains
active across a callback. Its enabled state has no public getter; the preservation
log deliberately makes no claim about that state. An uninitialized controller
defers while census/benchmark requests, watch or function tracing are active.
Otherwise its existing idle-owner controls initialize and enable it once.
Unlike clip-region fusion, this helper has no existing native-clip/register
configuration switch. No new environment gate is invented.

The selected log is `[polygon-edge-trial] ... startup enabled 1`. The existing
`[polygon-edge] N frames: M native calls` report establishes actual admitted
work; a build flag alone does not. After completion, startup adds only the
atomic completed check. Existing controller and diagnostic behavior remains
unchanged after selection, except that private-mode remote selector34 is rejected:
its legacy completion/cancellation would restore OFF. Existing clip selector 36
rejection remains controlled independently by `XV_CLIP_REGION_TRIAL`.

The strict Make flag requires `RECOMP=1 GAME_PROFILE=halo_ce_3925`, native
polygon-edge compilation, the object backend and compiled light-census admission
API. Runtime census remains OFF. Only `xd3d.o` and `xv_benchmark.o` receive the
new selector; a persistent config stamp covers ON/OFF transitions. Ordinary
default/OFF builds preserve the previous object bytes.

## Qualification and limits

The retained generated kernel, controller, header and generator match the original
qualified integration exactly. Current B77C0 is the original translated body plus
only the existing guarded native entry hook. The current ARM native body and
relocations exactly match the qualified object; the public wrapper differs only
by a resolved local BL instead of the fixture's function-section relocation.
Its native frame remains 360 bytes plus the8-byte wrapper, excluding descendants.
This is not a measurement of physical stack headroom.

The new startup fixture links both actual controllers, the actual object pool
and retained clip compatibility code. Ten fresh processes pass both regular
and ASan/UBSan runs, with 67 complete context/guest-arena/page-table/root/host-FP
checks per run. Cases exercise real worker callbacks, forged owner contexts,
foreign-thread invalid pointers, queue/service/fiber/guard rejection, pending
diagnostics, incompatible clip settings, and OFF/ON/active preservation for either
or both controllers. Platform fiber identity and diagnostic storage are explicit
test doubles; this does not prove physical Vita scheduling or gameplay stability.

Twenty-two Make parse cases cover strict values and prerequisites. Five ASan/UBSan
builds execute 284 request/poll checks across absent/0/1 and both trial selectors,
preserving rejected queues and control state. Six actual retained ARM transitions
restore exact previous objects when OFF, change only the two intended objects
when ON, and avoid rebuilding for repeated values. The other 92 object hashes
are preserved, not freshly recompiled. No full game build, packaging, device
operation, timing benchmark or redundant mathematical sweep was performed.

Reproduction tools are `test_polygon_edge_startup.py`,
`test_polygon_edge_trial_admission.py`, and
`test_polygon_edge_startup_build.py` under `tools/`. The first takes the retained
generated `xk_clip.c`; the last requires an independent disposable copy of the
current retained stage and its exact build command. Private evidence is under
`direct-cluster-query/polygon-edge-startup`. Ordinary gameplay after a confirmed
fresh launch must still establish actual coverage, stability and frame behavior.
