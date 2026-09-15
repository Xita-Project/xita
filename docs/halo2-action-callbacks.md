# Halo 2 action callback table

Native158 stops at original target `E8980`, called from `E6900` through
`[record+0]` at `E6917`, return `E6919`. The caller selects a record using the
first word of its original argument structure. The target belongs to record
`467584`, shared by action indices 8 and 18. Its result is consumed by the
original object code; discovery does not replace that result.

The original loop at `E6830` visits indices 0 through 59 of the pointer table
`4677C8..4678B8`. That bound agrees with the owned data: 60 entries share 38
four-word records in `467564..4677C4`; floating-point constants follow the
table. Four complete fingerprinted callers establish each record field:

| Original caller | Field | Original behavior retained |
| --- | --- | --- |
| `E6900` | `0` | Required call, original AL result and dependent notification |
| `E6830` | `4` | Conditional call for active bits; aggregate true results and clear false bits |
| `E6960` | `8` | Optional call followed by clearing the selected bit |
| `E69C0` | `Ch` | Optional call followed by clearing the selected bit |

Discovery validates each pointer against the separate record arena,
record-relative 16-byte alignment, `.data` raw bounds and a complete readable
span. Shared records are deduplicated. Field 0 must contain executable title
code; the other fields may be null, matching the callers. Nonnull targets
must be in executable `.text`. No adjacent constants, record padding or
unrelated table is treated as code. All original iteration order, arguments,
bit operations, callbacks and result handling remain translated.

The owned table contributes 69 unique callback roots, including the observed
`E8980`. Private proof is `object-lifecycle/action-rows-guards.json`,
`action-complete-walks.txt`, `action-table-references.txt` and
`action-roots.json`. The synthetic fixtures check all 60 table slots, all four
fields and fingerprints, required versus optional nulls, aliasing records,
invalid or truncated spans, alignment, code/data sections, ignored neighbors
and unchanged image data. No shared runtime, sound adapter or graphics
behavior is changed.

Native159 uses private `action-callbacks` output. Its diagnostic package
embeds owned game code/data and must not be distributed. A visible original
intro or additional translated functions do not establish a main menu.

All 47 focused callback, sparse-jump, profile and LOOP tests pass.
Regeneration adds 154 reachable functions, 1,828 blocks and 15,756
instructions. There are 12,382 generated functions and 3,825 unsupported
instructions; the latter count is unchanged. These are automatic discovery
and translation metrics, not executed coverage.

Native159 executes the action callbacks and reaches the next original
callback, target `D4E60` from `11C1B0`, return `11C21D`. The actual window
capture again visibly shows the Microsoft Game Studios intro:
`native-159-view/early-movie-middle.png`, SHA-256
`8bf81d15a2ae255ecaebe117d264701b0f6d5c226224d6842545ea63a67098d6`.
Final frame135 remains black and the decoded channel state is unchanged;
no original main menu is visible.

All 171 dependency targets are verified after the completed four-job build.
Native159 ELF SHA-256 is
`fc0b06c05c92fc6ce9ca24ae0eb41eddb05c71cbacc9d72fcb2f6ff206fd84ba`,
EBOOT `3acd57f39ee7988d403545ee66bd0613d7a98b100bcab00107c8edc8a8a7d2ae`,
trace `c336874007d8365332ce29ad24f476b1792ec67695c2cdd768462ac93d03abff`.
Evidence is private `native-159-artifacts`, `native-milestone-159.json` and
`native-159-view`. With the owned emulator stopped, preserve/reset the
private cache using `python3 preserve_fresh_cache.py native159-replay`,
then run `python3 capture_run.py 159-replay native-159-artifacts` and
`python3 drive_startup.py 159-replay native-159-artifacts`. These helpers
retain the original map copy and normal Start input. The native build and
owned emulator are stopped after capture.
