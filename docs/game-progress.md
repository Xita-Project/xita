# Per-game progress maps

The [game progress viewer](https://xita.dev/games/) and local developer report use
the same metadata and frontend. Select a game profile, search functions or
addresses, filter by status, click a block, or use the keyboard-accessible entry
list. **Explore source chunk** narrows the view; **Reset view** returns to all
recorded entries. The layout also works on phones.

## Generate a report

Use a generated output directory with a matching `recomp_report.json` and all
`code_*.c` chunks. The tool rejects another profile, a different executable hash,
duplicate functions, or missing/unexpected generated functions.

```sh
python3 tools/profile_progress.py report --profile halo_ce_3925 \
  --recomp-dir recomp --revision YOUR_BUILD_COMMIT \
  --output local/progress/halo_ce_3925.json
python3 tools/profile_progress.py site \
  --reports local/progress/halo_ce_3925.json \
  --output local/progress/viewer
```

Open `local/progress/viewer/index.html` directly in a browser. No server, npm
package, network request or game data is needed to view the generated report.
Only report generation reads the private generated source.

For multiple game profiles, generate one JSON report per profile, then pass all
of them to `site --reports`. The game selector is populated from those reports;
duplicate profile IDs are rejected. Different executable revisions should have
different profile IDs. The site generator can write to the separate website
checkout's `public/games/` directory using the same command.

`--revision` records the reviewed source revision used to build the inspected
artifacts. It does not independently prove build provenance. The report also
records the profile's XBE hash and a combined hash of the generated chunks. No
original instructions, generated C bodies, absolute local paths, game assets,
logs or saves are exported.

## What the colors prove

| Status | Evidence |
| --- | --- |
| Translated | A generated function exists and has no recorded unsupported handler. |
| Native helper present | A native entry helper can return before the original fallback. This includes conditional/deferred routes, not just math replacements. |
| Unsupported handler present | The function contains `xv_unimpl` sites that need review. |
| HLE / kernel boundary | The generated report references that API. Implementation coverage is not inferred. |

Area is based on unique emitted instruction markers **within each function**,
with a minimum area of one for entries without markers. Shared code can appear
in multiple functions. Select **One block per entry** for equal weighting.
These values are neither unique binary-byte coverage nor CPU cost.

Unsupported handlers do not establish reachable gameplay bugs; discovery can
include dead code or data. Generated functions do not prove behavioral accuracy,
exact compiler matching, or a completed port. The initial schema keeps
`exact_match` and per-function `validation` null until a later evidence import
can substantiate them. No completion percentage is manufactured from translation.

The first Halo snapshot, from the source/build reviewed at `22331f4`, contains
8,021 generated functions: 7,972 classified translated, 5 with native entry
helpers and 44 containing unsupported handlers. There are 203 HLE and 89 kernel
boundaries. The five entry helpers include deferred flares and four native math
routes. These are development counts, not measured FPS improvements.

## Checks

`python3 tools/test_profile_progress.py` checks count deduplication, profile and
artifact mismatch rejection, no false validation claims, metadata-only export,
multiple profiles, and script-safe serialization. An optional Playwright check
exercises the actual viewer:

```sh
node tools/check_progress_browser.cjs \
  file:///ABSOLUTE/PATH/local/progress/viewer/index.html /tmp/xita-progress-check
```

Set `PLAYWRIGHT_MODULE` to an existing Playwright installation if needed. The
browser check covers the current Halo snapshot, search, filters, canvas/list
selection, source-chunk exploration, empty results, pagination, keyboard use and
responsive widths from 320 to 1440 pixels. Tests use synthetic fixtures for
multiple profiles; they do not imply another Xbox title currently works.
