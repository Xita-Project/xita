# Third-party components and provenance

Xita's original code is **GPL-3.0-only**; see [LICENSE](LICENSE) and [NOTICE](NOTICE).
This inventory distinguishes tools used to build Xita from material that may be
included in a distribution. It does not relicense someone else's work.

| Component | Use in Xita | Distribution notes |
| --- | --- | --- |
| [iced-x86](https://github.com/icedland/iced) | Python instruction decoder used by the offline recompiler | Installed separately; MIT. The upstream notice is preserved in [LICENSES/iced-MIT.txt](LICENSES/iced-MIT.txt). |
| [VitaSDK](https://vitasdk.org/) | Compiler, open homebrew headers, import stubs and support libraries | Installed separately. Components have their own terms; retain the notices for the exact SDK/libraries used when distributing eligible binaries. A single blanket "VitaSDK license" is insufficient. |
| [XbSymbolDatabase](https://github.com/Cxbx-Reloaded/XbSymbolDatabase) | Symbol database format accepted by the recompiler; existing `halo_symbols.json` needs an exact-source provenance record | Upstream is MIT; its notice is in [LICENSES/XbSymbolDatabase-MIT.txt](LICENSES/XbSymbolDatabase-MIT.txt). The current symbol export is held out of the source review export until its originating revision and production method are recorded. |
| [extract-xiso](https://github.com/XboxDev/extract-xiso) | Optional extraction tool for a user's own image | Installed separately, not bundled. Follow that project's license if redistributing it. |
| [Vita3K](https://github.com/Vita3K/Vita3K) | Separate emulator for development and validation; published GXM format definitions consulted by tests | Not bundled or linked into Xita. Hardware results must be reported separately. |
| [xemu](https://github.com/xemu-project/xemu), [Invader](https://github.com/SnowyMouse/invader), [SDL](https://github.com/libsdl-org/SDL) | NV2A register identities, widget format definitions and Vita audio buffer handling consulted during the weapon/menu/audio audit | Reference projects, not bundled or linked. This change does not copy their implementations; record any future code reuse and its license separately. |
| [stianeklund/halo](https://github.com/stianeklund/halo) | Camera/frustum descriptions and function names consulted for the [native visibility experiment](docs/native-bounds-20260908.md) | Reference only at revision `a613ab54a68366ad1053d1aa913ddf687a14df8e`; no implementation copied or bundled. Executable fingerprints differ. No project-level reuse license was found during this review. |
| [vcp](https://github.com/isage/vcp), [PVR_PSP2](https://github.com/GrapheneCt/PVR_PSP2) | Published dump layouts and register definitions consulted by the independent crash inspection tool | Reference sources are cited in the tool. This review did not establish an exhaustive provenance chain for every implementation; record any copied code and its terms before release. |
| Dashboard font | Small bitmap glyphs generated from patterns in `dashboard/gen_font.py` | The current generator declares original glyph patterns and imports no external font asset. Historical revisions still require review before exposing Git history. |
| Game-derived files | Recompiled C, translated shaders, shader definitions, game images/maps and captured scenes/media | Excluded from Xita's GPL grant and the source review export. Do not distribute merely because they were translated or captured by this project. |
| Sony/Xbox proprietary components | Firmware modules, official SDK tools, original libraries and executable content | Not covered by Xita's license. Do not bundle or link downloads to unauthorized copies. |

The repository's earlier legal statement that it contained no game content was
too broad: translated shaders and game captures are tracked. See the
[release audit](docs/release-audit.md) for findings and the remaining gates.

AI-assisted work does not establish provenance or permission. Contributors must
identify external sources and preserve their notices; the GPL applies only to
rights they can actually grant.

## Release updater TLS trust store

`resources/release-ca.pem` is the Mozilla CA bundle converted by curl, retrieved
2026-09-26 from https://curl.se/ca/cacert.pem. SHA-256:
`a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505`.
It is unmodified and licensed under MPL-2.0; see
[LICENSES/Mozilla-MPL-2.0.txt](LICENSES/Mozilla-MPL-2.0.txt).
Source and conversion details: https://curl.se/docs/caextract.html.
The tester updater uses pinned libcurl and Mbed TLS builds plus VitaSDK zlib.
Their license texts are included in `LICENSES/`. Mbed TLS is used under its
Apache-2.0 license. The dependency build script records the source archive
hashes and the Vita configuration changes.
