# Halo 2 native input boundary, September 13

Native attempt 39 passes the Xbox USB startup boundary through six pinned XPP
APIs backed by `sceCtrl`. The original game initializes the adapter, observes
its insertion mask through the original query/change code, and opens port zero.
It then enters sound initialization and stops at undiscovered indirect target
`00379D0C`, called at `0037B17B` through vtable `004170E4` slot one. There is still
no menu or geometry; the only presented framebuffer is the earlier black
startup buffer. The final capture shows Vita3K back in its library after the
explicit stop, not game content.

The six entry ranges and fingerprints live in `HOST_BOUNDARIES`. This is only
enabled by the existing opt-in Halo 2 host-channel profile; CE input and the
default generator are unchanged. XInitDevices retains the observed void ret8
contract and accepts the exact three preallocation entries requested by this
image. Complete mapped input/type/output spans are checked before host polling
or guest mutations. A real successful `sceCtrlPeekBufferPositive` sample is
required; a host error stops execution.

One built-in controller is exposed on port zero. Original XPP records contain
current/change/previous masks `1/1/0`; voice and memory units remain absent.
Open/close maintain explicit host handle lifetime. State uses the actual
22-byte guest layout and increments its packet only when input changes.
Capabilities use the actual 25-byte layout. Face buttons, D-pad, start/select,
triggers, and analog sticks map directly to Vita controls. Black/white and stick
click buttons are absent. Rumble capabilities are zero. Feedback validates the
whole 70-byte structure and stores/returns error 50, matching the original
driver's no-output-endpoint path. It promises no asynchronous completion or
event signal. Other unsupported initialization/polling modes stop explicitly.

The owned-image call/layout audit is corroborated by the original Xbox types in
[Cxbx-Reloaded Xapi.h](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/master/src/core/hle/XAPI/Xapi.h).
No CE controller adapter is reused: those adapters contain CE game addresses
and frame/replay policies. The new API handlers preserve all guest CPU/FP state
except the documented return register and stack consumption.

Validation: all 14 host executables pass, including input state/layout, changed
packet, absent devices, stale/foreign handles, close/reopen, host failures,
noncontiguous and incomplete page spans, exact output bounds, and full CPU
state checks. The input tests also pass ASan/UBSan. All 18 Python compiler,
profile, bus, callback-root, guest-address, LOOP, bit-string, and SIMD tests
pass. Native build uses the existing private generated target with guest O0,
runtime O1 and four jobs.

Private evidence: `native-39-artifacts`, `native-39-view/window-4.0.png`,
`native-milestone-39.json`, `input-tests.log`, `input-sanitize.log`, and
`input-regressions.log`, under the isolated Halo 2 private directory.

| Native 39 artifact | SHA-256 |
| --- | --- |
| ELF | `435bec448c1932cb0f75281ff5ee9c182a834e6d3d60436f4eda76276fac9e77` |
| EBOOT | `2c2bb72fac95ac7dc10d9bf9123ea4adffbc570e6e95cdfc37ff706c10c01692` |
| Boot trace | `7ee6efdc95fe5cdcd90f1104168b59598be01cb6a40363dd763d98060469b794` |

The diagnostic VPK embeds owned game image/code. It must remain private and
must not be uploaded as a distributable release.
