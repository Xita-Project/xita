# Blood Gulch Warthog / rocket freeze — September 26

User reproduction: drive the Warthog, then shoot rockets at it to test physics.
The installed build was **0.2.0-perf.253**, before any tester-updater or dashboard
changes were deployed. The latest log, preceding log, freeze marker, core dump
and six screenshots were archived privately before reopening the application.

The watchdog reported no presented frame for 40 seconds at frame counter 12346,
process time 859.3 seconds, then intentionally trapped to produce the core dump.
The final screenshots show the sky and a flat pale field with world geometry
absent. They are not evidence of an ordinary steady-state FPS result.

Symbolizing against the matching perf253 ELF places the running guest thread in
`f_00091EE0`, with `f_000939C0` and `f_00180ADA` in the stack scan. The render pump
is waiting. This establishes a guest-side investigation target; a stack scan
alone does not prove the cause or establish that the GPU driver crashed.

Next: recover the stopped PC/registers and the list state traversed by `91EE0`,
check for an invalid/cyclic chain and its writers during vehicle explosions,
then reproduce with bounded diagnostics. Do not hide a corrupt chain by silently
skipping objects or ship a speculative physics fix with the dashboard work.
