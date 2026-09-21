# CPU clock ownership

When an external overclocking plugin manages Xita, set `XV_CPU_EXTERNAL=1`
in `ux0:data/xita/env.txt`. This option leaves CPU clock requests to that
plugin: Xita skips its boot CPU request, game CPU request and kernel-helper
load/bind. GPU, bus and crossbar settings are unchanged. The default is `0`,
which preserves the existing Xita policy.

Use a full Vita power-off/restart once when switching from Xita's resident
clock helper to external control. Closing or updating the game does not unload
the kernel module, and skipping a new bind does not disarm an already-running
helper. This option does not stop/unload that module or modify plugin settings.
After reboot, keep the external plugin's per-game CPU profile enabled.

The private remote `/status` response includes `reported_cpu_mhz`, queried from
the Vita's user clock API at request time (0 in host tests). It is an API report,
not a measured CPU cycle rate. Plugin target settings, user/kernel API readings,
and actual clock behavior can differ; do not infer FPS gains from a target label.

The present Xita helper only calls the kernel power setter. It does not contain
the lower-level clock adjustment used by [PSVshell](https://github.com/Electry/PSVshell/blob/master/src/oc.c).
On perf79, both API getters reported 444 MHz after the helper accepted a 500 MHz
request. The user reports an external tool already set to 500 MHz. A possible
interaction between the two controllers remains unverified.
