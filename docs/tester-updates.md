# Tester builds and release updates

Tester builds are the default (`XV_DEVELOPER_BUILD=0`). They omit the development
HTTP server, remote controls, screenshots, log downloads, environment editing and
incoming executable uploads. An existing `XV_REMOTE_TEST=1` setting or pairing
key cannot enable those features. This does not remove separately installed
VitaShell or vitacompanion services; Xita does not manage console plugins.

Developer builds require `XV_DEVELOPER_BUILD=1` and retain the existing paired,
opt-in LAN service. Use that build on the development Vita. Build-mode changes
invalidate affected objects and the package's compatibility contract. Do not
publish developer VPKs. Halo 2's separate experimental executable has not yet
been audited for this tester boundary, so tester packaging currently rejects a
bundled Halo 2 executable; combined developer builds remain supported.

## Updating on the Vita

Open **Updates**, select **Check for updates**, read the available version and
release summary, then select **Download update**. Downloading alone does not
install anything. Once verification succeeds, select **Install and restart**.
**Restore previous build** selects the previous confirmed executable.

The updater makes outbound HTTPS requests only after a local menu action. It
uses the fixed `Xita-Project/xita` GitHub Releases location, verifies certificates
and hostnames using its packaged Mozilla trust store, and accepts HTTPS
redirects only. It sends no pairing key, GitHub token, game data or logs.
The release owner and GitHub HTTPS delivery are the publisher trust boundary;
SHA-256 verifies content integrity, not an independent publisher signature.
Keep the Vita's date correct for certificate validation.

A missing/private release produces an unavailable-update message. No repository
credential is embedded. Repository privacy is never changed by this feature.
Only published release assets are downloaded; source commits and branch heads
are not installable updates.

The existing installer checks the asset contract, executable size and SHA-256,
then installs into the inactive slot. Saves, settings and game data are outside
that transaction. A failed candidate boot falls back on the next bubble launch.
A release that changes the launcher, trust store, shaders or other assets needs
a **full VPK installation through VitaShell**; the dashboard reports this rather
than attempting a partial upgrade. First-time migration to tester builds also
requires the new VPK. The updater is currently for Halo CE executable updates.

## Preparing a release (local only)

Build the pinned HTTPS dependencies into a local directory:

```sh
python3 tools/build_release_deps.py --output build/release-deps
make RECOMP=1 XV_DEVELOPER_BUILD=0
python3 tools/prepare_release_update.py xita.vpk \
  --tag v0.2.0-test.3 --version 0.2.0-test.3 \
  --summary 'Describe the fixes in this release.' --output /tmp/xita-release-assets
```

Use the game's existing qualified build flags in addition to those shown above.
The asset preparation script rejects developer/unknown runtimes, duplicate ZIP
entries, oversized packages and inconsistent contracts. It never uploads or
publishes anything. Release maintainers must attach `xita-update.txt` and
`xita-runtime.self` to the **same tag**, alongside the single normal tester VPK.
Do not replace assets on an already published tag. Version numbers must match
the runtime and increase for each published release. Review release artifacts
and applicable source/license obligations before publishing.

GitHub's `releases/latest/download/xita-update.txt` selects the published latest
release. The metadata pins a tag, so the subsequent runtime download does not
race a change to which release is latest. Full release notes remain on GitHub;
the dashboard shows a short summary and its installed version in the footer.

## Validation

`tools/test_release_update.py` exercises the production downloader state machine
with a deterministic transport, its HTTPS verification policy, corrupt payload
rejection, explicit installation, rollback and unavailable/invalid metadata. It
also compiles the default remote object and checks it has no external calls or
server endpoint data. These are host tests, not proof of a hardware HTTPS update.
The existing update tests cover interrupted transfers, corrupt metadata, failed
writes and missing boot confirmation. Dashboard tests cover navigation, game
selection and settings preservation.

Native tester and developer VPKs compile successfully. The default tester remote
object has no external calls, and the package contains only the tester identity.
The developer dashboard preview is boot-confirmed on hardware as perf254.

A published compatible test release and an on-device HTTPS download/install/
rollback exercise are still required before distributing this updater to testers.
No release is published automatically by these tools.
