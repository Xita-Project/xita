# September 10 USB vertex worker update

The [parallel vertex upload candidate](vertex-upload-worker-20260909.md), source
`21fe9caf992b6c7f77bc2d4350ddd53de95ad987`, is installed on the connected Vita.
The existing executable, configuration, logs and 20 recent screenshots were
backed up and hash checked before writing. All 1,584 supporting package files
matched the candidate, so only the executable and two configuration entries
needed updating. No VPK installation was required.

The executable and configuration were staged and flushed, replaced, then checked
again after unmounting and remounting the USB volume. USB was safely unmounted
after verification. Installed `eboot.bin`: 31,103,114 bytes, SHA-256
`b4eeb211b06641ed53ddae8d6c9e5078b73bcbf4d30a33710e0e50645f169106`.

## Settings and hardware test

`XV_VERTEX_WORKER=1` enables the worker for ordinary play.
`XV_BENCHMARK_VERTEX_WORKER=1` selects its off/on/off comparison, taking precedence
over the older vertex-reference comparison. All other settings were preserved,
including 360p, Low model detail, 128-pixel texture limit, triple buffering off,
the 20 FPS cap and the 500 MHz CPU request. The project default remains off until
physical testing establishes whether this worker helps.

1. Relaunch Xita and load a quiet first-person gameplay view. Face a scene that
   normally runs below the 20 FPS cap; looking at the ground could hide a gain.
2. Press **L + R + Square** together, then release. Leave the camera still until
   all three phases finish and the test indicator disappears. Allow roughly
   one to two minutes at 5–10 FPS. Adding Select also works; L + R + Select
   without Square selects the separate resolution test.
3. Play normally afterward, then reconnect USB to collect the logs. Completion
   or cancellation restores the configured worker-on mode.

Compare total frame times and the two off phases, then stream preparation,
worker throughput and upload completion waits. Increased core-0 activity alone
does not establish better performance. Moving AI and physics to independent
threads remains separate work; this candidate only moves owned vertex copies.

## Incoming log

The backed-up run predates this worker and contains no controlled comparison.
Across its last 60 available CPU samples, median system busy readings are C0 6%,
C1 9% and C2 92.5%. The last 30 timing windows range from 4.9 to 10.7 FPS while
the player changes views. Their stream preparation category ranges from 3.487
to 35.766 ms/frame; that category includes work which remains on the caller.
These are workload observations, not a before/after result or predicted gain.

The final timing window reports game 103.5 ms, Present wait 1.2 ms, and 9.6 FPS.
Present timing does not measure total GPU execution or utilization. The next
hardware comparison will determine whether overlap reduces frame time or simply
adds memory traffic and synchronization overhead.

Native, host and emulator validation is recorded in the candidate note. Hardware
stability and performance remain unverified. This executable update includes no
LiveArea asset changes and does not claim to fix the reported wallpaper problem.
