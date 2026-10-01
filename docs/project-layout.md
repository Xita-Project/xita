# Project layout

[README](../README.md) · [Build guide](building.md) · [Architecture](modular-architecture.md)

| Folder | Contents |
| --- | --- |
| `runtime/` | Native Vita application, renderer, resource handling and workers. |
| `recompiler/` | Offline XBE, x86 and shader pipeline; shared Python modules in `core/`. |
| `games/` | Revision-pinned game profiles and reviewed adapters. |
| `recomp/` | Xbox execution support, shared HLE in `kernel/`, host checks and locally generated guest code. |
| `dashboard/` | Launcher and settings UI. |
| `tools/`, `tests/` | Development utilities, regression checks and fixtures. |
| `docs/` | Installation, development, hardware reports and README screenshots. |
| `shaders/` | Shader sources and local generated/compiled programs. |
| `sce_sys/`, `assets/` | Application presentation and development assets. |
| `LICENSES/` | Third-party notices. |
| `site/` | Archived early website draft. |

The README, roadmap, compatibility information, contribution rules, licenses
and Makefile remain at the top level. Run build commands from that directory.

## Local files

These folders are ignored and do not belong in source commits:

- `local/halo_ce_3925/`: executable manifest, symbol inputs and captured shader metadata.
- `local/tools/`: locally supplied helper binaries such as `extract-xiso`.
- `haloce/`: the user's supported executable and maps.
- `build/` and other build directories: objects, archives and compiled programs.

Existing local inputs are preserved when moved into `local/`. Removing them
from the current Git tree does not remove earlier Git history. The repository
remains private; the [release audit](release-audit.md) still applies.
