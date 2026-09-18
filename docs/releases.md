# Releases and downloads

[Install Xita](installing.md) · [All releases](https://github.com/Xita-Project/xita/releases)

**One build per release, with one installable VPK.** Download links in the README
and installation guide lead to the current game package. Networking diagnostics
have their own release and bubble.

| Download | Purpose |
| --- | --- |
| [Xita 0.2.0-test.1](https://github.com/Xita-Project/xita/releases/tag/v0.2.0-test.1) | Current tester package: cumulative CE performance work, game selection and visible version/revision. See tester notes for validation limits. |
| [Xita AdHoc Test 01.01](https://github.com/Xita-Project/xita/releases/tag/adhoc-20260908b) | Standalone diagnostic for two Vitas; wireless testing pending. |

Each release has three uploaded files: the VPK, `SHA256SUMS.txt` and
`BUILD-INFO.json`. GitHub also adds source archives automatically; these cannot
be installed on a Vita. These remain development prereleases in the private
repository.

Release identity is defined in `version.json`; source revisions are embedded in
the dashboard, performance overlay and remote status. [Current tester notes](tester-build-20260918.md).

## Older builds

Older releases are marked **Superseded** and link to the current download.
Their VPK bytes are preserved for recovery and test comparisons.

| Build | Release |
| --- | --- |
| Instrumented profile/LiveArea build | [September 9](https://github.com/Xita-Project/xita/releases/tag/dev-20260909-livearea-profiles) |
| Indexed vertex comparison | [September 8](https://github.com/Xita-Project/xita/releases/tag/dev-20260908-vertex-references) |
| Native visibility experiment | [September 8](https://github.com/Xita-Project/xita/releases/tag/dev-20260908-native-bounds) |
| First in-game graphics panel | [September 8](https://github.com/Xita-Project/xita/releases/tag/dev-20260908-settings) |
| Rendering and modular libraries | [September 8](https://github.com/Xita-Project/xita/releases/tag/dev-20260908) |
| Earlier installed recovery build | [September 8, 07:00 CDT](https://github.com/Xita-Project/xita/releases/tag/dev-20260908-recovery) |
| Original ad hoc tester — failed hardware startup | [Superseded diagnostic](https://github.com/Xita-Project/xita/releases/tag/adhoc-20260908) |

The September 8 packages were originally grouped under one tag. Their individual
releases now separate them without rebuilding the VPKs. The original
`dev-20260908` tag is unchanged. Build information retains the distinction between
source commits, packaging snapshots and locally generated game inputs.

## Preparing the next release

1. Give each build a unique tag, such as `dev-YYYYMMDD-description`. Use a
   separate `adhoc-YYYYMMDD-revision` tag for the networking diagnostic.
2. Attach one VPK, its `SHA256SUMS.txt` and its `BUILD-INFO.json`. Record the exact
   executable/source provenance, settings and completed tests. Distinguish
   emulator checks from physical Vita results.
3. Lead the release notes with the VPK filename, changes, installation link and
   known limits. Keep a development build marked as a prerelease.
4. Verify uploaded sizes and SHA-256 digests. Do not replace a published VPK or
   append another candidate to its release; a changed build gets another tag.
5. Update the current game link in the README, installation guide and this page.
   Mark the previous game release **Superseded** and link forward. An ad hoc
   release must not become the default game download.

Repository visibility stays private. Public-release preparation remains covered
by the [release audit](release-audit.md).
