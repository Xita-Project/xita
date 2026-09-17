# Cumulative scene and object-pass attribution

The next hardware build retains all twenty-one selected optimization paths and
adds the reviewed scene/pass counters. Source `80785ac` builds runtime
`4d099facbd4e1a6987ab71d2569a79cfa921d07de4ade91f9a517caf2f5a1b6b`.
The remote updater confirmed its boot in slot 1; preceding `bc7c9884` remains
in slot 0 for rollback. No graphics settings or worker policy changed.

Only `code_010.o`, `xk_owner_phase.o` and `xk_object_jobs.o` differ from the
preceding package; 91 other objects remain identical, including the qualified
query and solver. All 1,588 package members retain the update contract. Only
the game executable and boot selection differ. Selective generation preserves
every original guest instruction and unrelated source body; compiler decisions
can still alter other function bytes within the selected scene unit.

Fresh startup reached the normal menu and New001 Normal campaign checkpoint.
Nine consecutive matching stationary reports at camera `-28.66,32.52,0.62`,
direction `0.56,0.82,-0.15`, have valid scene and accepted-pass accounting.
Both object-worker IDs produce valid raw run-clock deltas without API errors.
These observations use ordinary gameplay with the benchmark inactive.

| Median elapsed milliseconds per displayed frame | Time |
| --- | ---: |
| Whole frame | 80.10 |
| Tick driver, including nested work and waits | 36.40 |
| Scene dispatcher, including nested work and waits | 41.13 |
| First scene section, within the dispatcher | 25.56 |
| Second scene section | 8.69 |
| Third / fourth / fifth / final scene sections | 0.06 / 1.47 / 3.17 / 0.004 |
| Accepted object passes, within the tick driver | 28.23 |
| Joined work from those same passes | 27.59 |
| Paired pass-minus-join difference | 0.60 |

These rows overlap and must not be added. Medians of separate rows need not
sum or subtract exactly. Scene intervals include draw preparation and waits;
raw thread-clock values are not converted to CPU percentages. The selected
object rows have consecutive matching reports, so a skipped object-report
window is not treated as a single nominal 60-frame interval.

The first scene section is now the main attribution target. It includes
visibility, model preparation, lighting and flare preparation; this does not
establish that its full 25.56 ms is CPU work or removable. The small paired
object-pass residual puts scanning/queue setup below the larger scene section
in priority. Work outside the accepted object pass still needs attribution.

Two fire inputs, camera/strafe, forward/reverse movement and pause completed.
The campaign capture at frame 13,296 contains 2,150,626 log bytes with no searched
fault marker and no logger error. Movement changes the scene distribution; it
is not pooled with the stationary checkpoint. This is a bounded smoke check,
not long-session crash clearance or a demonstrated FPS gain. The checkpoint
median is 12.5 FPS; stable 20 FPS and another five FPS remain unverified.

Private package, installation, screenshots, controller journal, complete logs,
selected rows and analysis are preserved in `scene-pass-startup`. The preceding
combined build's Blood Gulch combat/driving check is recorded in
[the cumulative trial](cumulative-render-update-20260917.md).

## Blood Gulch follow-up

The same process entered New002/New003 Blood Gulch through the normal menus,
turned toward the valley, walked forward, charged/released the plasma pistol
and paused. The final frame-31,252 capture contains 5,620,871 bytes with no
searched fault marker or logger error. The 25 base-facing, 16 valley-facing
and 33 moved-valley stationary windows have valid consecutive main scene/pass
reports. Moving, loading and paused windows are excluded from these groups.

| View | Median FPS | First scene section | Whole scene |
| --- | ---: | ---: | ---: |
| Near blue base, facing the base | 13.6 | 22.07 ms | 41.88 ms |
| Near the same location, facing the valley | 8.35 | 51.98 ms | 71.44 ms |
| After walking toward the valley | 10.3 | 41.99 ms | 57.37 ms |

These are different views of one executable, not before/after performance
results. The recorded flare-consumer wait has median zero in all three groups.
That excludes this particular wait as the explanation for these samples, not
all GPU costs. The first scene section remains the largest attribution target.
The next optional [five-cut refinement](scene-bucket0-detail-20260917.md)
separates its visibility, model, lighting, ordered callback and flare work.
