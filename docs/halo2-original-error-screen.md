# Halo 2 original error-screen presentation

Native64 presents the original game's disc-error screen in Vita3K. The actual
`native-64-view/window-4.0.png` desktop capture reads: “There's a problem with
the disc you're using. It may be dirty or damaged. Press A to continue.”
This is **not the main menu**, and the message does not establish that the
owned assets are damaged. The reason the game selected this error path is
still being traced. The explicit unavailable-audio diagnostic remains enabled.

The second device's interval-one flip first retires on real Vita vcount
`84 -> 85`, with original callback record `{1,1,1}`. The original caller then
resets its notification event and waits at `3F9BF7`. The adapter supports
that exact second initialization wait: a real vblank `85 -> 86`, the original
empty-queue consumer, original deadline update, actual event signaling, and
original callback record `{2,1,2}`. Original game count increments `0 -> 1 -> 2`.
Unknown callbacks, active display, other queue/timing state and further waits
remain outside this boundary. Host wait failures preserve pending guest state.

The original `AvSetDisplayMode` call then supplies 480p linear A8R8G8B8 buffer
`038E8000`, 640x480, pitch 2560. The existing converter and real Vita presenter
apply it at host vcount 87. No replacement UI is drawn. The strict next stop is
`AvSendTVEncoderOption(11, 3, NULL)` at return `1415D`, requesting a different
flicker-filter level from the initialization setting 5. No geometry, texture
execution, main-menu map load or main menu has been demonstrated.

A new stop-only diagnostic saves `last-presented-at-stop.bin`, preserving the
latest successfully presented buffer without file writes in the recurring
presentation path. It can retain a buffer while the display is blanked, so its
name does not assert current scanout enable state. Native64's header is
`{960,544,3840,2}` and its 2,088,960 pixel bytes contain 519,442 black pixels and
2,798 gray text pixels. This distinguishes frame 2 from the old entirely black
first-frame capture. The desktop capture independently confirms presentation.

All 18 host executables plus the timed case pass; both runtime cases also pass
ASan/UBSan. The timed case checks both wait failures/no counter advance, exact
callback record order, original queue return contracts, event-before-callback
ordering, full interrupted CPU/FPSCR state, parser retry and mode application.
Native64 separately executes the original queue/callback bodies. This remains
a bounded synchronous callback adapter, not a general interrupt-priority model
or continuous vblank implementation.

| Private native64 artifact | SHA-256 |
| --- | --- |
| ELF | `2aa275882ca2ea05c2f5dce58d84da172121c4b5c82b8e2b5013e28ac76ec79d` |
| EBOOT | `deda36dcce888914ccc788f0116af7a102e7bc97a693807044a366c9969eecb9` |
| VPK | `4a976dc75eb6bfbc78ccdc3edf7c2ded6b69b987b5f574af0dc6336a5d709e57` |
| Boot trace | `7a87492533767b1391bf94a6a3f1cab3090091b2f8373bd248c08f8f537ad944` |
| Decoded channel | `c031753023b336b9b87f56009ab65edce7fe744c78e14ea45b26017dea2e33fd` |
| Last presented buffer | `a7d8be6fbe31bcc71b47ffc9d958471253719e82a3f032d29bf5af3727260abf` |

Replay from the source directory:
`python3 ../private/run_lab.py replay64 ../private/native-64-artifacts/halo2-boot.vpk`.
Generated source remains frozen at `online-interfaces/generated`. The package
uses the unavailable-audio and limited native FP probes, embeds owned game
code/image, and must not be distributed or uploaded as a release. All raw game
buffers, generated code and captures remain private.
