# Xita 0.2.0-test.1

September 18, 2026 · private development prerelease.

This build brings the accumulated Halo CE performance changes and experimental
Halo 2 source into the same development branch. It adds **Select Game**, a
version and source revision in the dashboard footer and CE performance overlay,
and matching fields in remote status. The previous working runtime remains
available through the updater's rollback slot after a compatible runtime update.

## Changes

- CE retains the cumulative native geometry, collision and worker changes.
  New visibility batches share copied inputs with workers; compact vertex
  capture and exact-byte reuse avoid redundant preparation where qualified.
- Halo 2 can be selected when its separate application and menu map are installed.
  Its code, data and save directory remain separate from CE.
- Integration preserves CE's existing save-signing key. Halo 2's nonzero
  virtual-console key applies only to its own build.
- Version `0.2.0-test.1` and the short source revision identify the running
  executable. A `+` suffix marks modified tracked source.

## Testing

Use native resolution and your normal graphics settings for the first session;
write down any changes. A few minutes of ordinary play are more useful than
repeatedly changing settings between unrelated scenes. No built-in benchmark is
required for this release.

1. Confirm the version in the dashboard. Select CE and launch normally.
2. In Blood Gulch, look across the valley, turn the camera, fire the assault rifle
   and plasma pistol, pick up a rocket launcher and drive a Warthog.
3. In campaign, try NPC combat, effects, a checkpoint and a cutscene transition.
4. Capture the performance overlay if something changes. Report version,
   revision, map, resolution, settings, symptom and steps to reproduce it.

The next target is sustained **20 FPS**, followed by **30 FPS**. Recent user
observations include around 12 FPS in the valley and 20 FPS in some other views;
they are not a whole-game average or proof of this build's benefit. The cumulative
capture/visibility changes need representative hardware acceptance.

Host tests cover the dashboard's selection/persistence/missing-install paths,
graphics settings, shared runtime, profile-specific signing keys, remote updater
and Halo 2 components. See the release's `BUILD-INFO.json` for exact build hashes,
completed hardware checks and outstanding validation. No CE Vita3K performance
validation is used.

## Files and limits

Download **xita-0.2.0-test.1.vpk** from its release. Keep existing CE images, maps,
settings and saves. [Installation instructions](installing.md).
Halo 2 has a [separate experimental setup](halo2-hardware.md); it is not a
supported playable title yet. Ad hoc multiplayer and complete campaign playthroughs
remain unverified. The software is GPL-3.0-only; game content has separate rights.
