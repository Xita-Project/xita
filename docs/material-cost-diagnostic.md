# Selected material shading-cost diagnostic

`XV_MATERIAL_COST=1` is an optional, deliberately image-changing diagnostic.
Default is off. Restart the game after changing it. It selects mode 7 only
where the existing mode-6 captured material proof and GREATER alpha-test guards
already pass, and excludes depth-prepared draws. It does not replace all shaders.

Generate a distinct `_axisblack_gt_cost.frag.cg` from the owned, qualified
`_axisblack_gt.frag.cg` with `tools/specialize_ps_cost.py`. Compile using the
same compiler options as the original. The optional GXP is embedded by
`tools/embed_ps_shaders.py`; absence or link failure falls back through modes
6, 5, and 0. Cache entries and failed-load records are distinct.

The generator changes only final RGB to gray, preserving the original alpha
computation and GREATER discard text. Draw order, blend policy, depth state,
CPU material preparation, and vertex program selection remain unchanged.
Compiler dead-code elimination can remove unused RGB samples and arithmetic.
This is not a visually correct optimization and must not become a default.

Interpret a hardware improvement as evidence that the selected shading path
(including texture traffic and potentially reduced varying/link work) matters.
It is not a pure ALU measurement. Changed colors can affect downstream blending.
No improvement does not exclude other GPU costs, other shaders, or CPU/GPU
contention. Compare settled ordinary gameplay, preserving frame tails; restore
the playable shader before gameplay qualification.

Validation so far: host ASan/UBSan production-linker fixture covers distinct
cache identity, original alpha policy, missing diagnostic fallback to mode 6,
further fallback when mode 6 is absent, cached failure, and wrong-key rejection.
Generator tests cover exact RGB-only change and rejection of unsupported alpha,
discard, depth-output, and unqualified-stage input. Generated private shader
source successfully. Isolated XVSC compiled the diagnostic GXP (456 bytes),
and perf252 builds successfully with byte-verified embedded shader data. The
update contract is preserved and only game-a.self/boot-game.txt change from
perf251. Hardware linking, visual inspection, and timing remain pending.
