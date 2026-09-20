# Bounded shader diagnostic lookup

After synchronizing a changed pixel shader, the runtime checked every previously
logged shader hash before deciding whether to emit its definition. The perf.38
campaign capture contains 39 distinct definition records: the capture had not
reached its 512-entry stop, so these lookups remained active during gameplay.

The diagnostic now uses a 1,024-slot open-addressed set with explicit occupancy.
It preserves full 32-bit hash equality, including hash zero, and the original
first-512-distinct-hashes limit. At that limit it returns before probing. Shader
identity, colors, state synchronization and GPU submission are unchanged. This
reduces a linear diagnostic lookup; no hardware frame-time gain is established.
The storage is 4,228 bytes instead of the old 2,052-byte history/count.

Validation covers duplicates, random sequences, zero hashes, deliberate collisions,
probe wrap and an immutable saturated table under ASan/UBSan. The production
shader-sync integration test passes 18,000 updates with its identity cache enabled
and another 18,000 disabled, including exactly 512 definition logs and one limit
message. Runtime compilation has generated header dependencies; the host test
also explicitly depends on the new header.

This change is intended for the next cumulative build together with the
[shader-constant page fix](constant-upload-pages-20260919.md). Perf.38 remains the
installed build until an updater receipt confirms its successor.

## Perf.39 deployment

Perf.39 (`fa8c968`) includes this lookup change and the shader-constant page fix,
with the preceding cumulative options retained. Only `game-a.self` and
`boot-game.txt` differ from perf.38. The updater verified runtime SHA-256
`acb0c7c6e5f8f5d0312b6b6f3b30de957bf58f8f4a030a3ed9e7e73aa1bef8c7`
and confirmed slot 1 after restart. The dashboard shows the correct version.
Private build/package/deployment receipts are in `../material-fixes-hardware/`.
The ordinary campaign launch sequence completed; gameplay qualification follows
separately. No automated FPS comparison was requested or run for this build.

The saved campaign reached loaded/active gameplay after restart. The screenshot
shows world geometry, pistol/arms, HUD and reticle at the original checkpoint
camera. Its instantaneous overlay reads 13 FPS; this is not a sustained average
or a demonstrated gain. No fatal/GPU-fault/data-abort marker was found in the
short captured log. Long-session, combat and heavy-scene performance remain
unverified. All launch controls were released.

Evidence: `gameplay-check.log`, `gameplay-status.json`, `gameplay.log` and
`gameplay.png` in the private deployment directory. Perf.39 is the current
boot-confirmed build; perf.38 remains the confirmed fallback.
