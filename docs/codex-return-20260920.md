# Codex return — September 20

Resumed from Claude's authoritative checkout at eebd698 after the user requested continued work. Read the latest handoff through section 33. Hardware status confirms perf64 / ac5e35c+, not perf48. No executable or clock settings changed during this investigation.

Private evidence: ../codex-return-20260920. Initial screenshot shows the campaign pause menu in a different room with 279 draws/frame; recent paused windows report 8.1–8.6 FPS and 18.5–18.7 ms draw-HLE. These do not establish a regression against the earlier stationary checkpoint. Saved and quit through the game menu, then reopened the campaign. The old menu sequence stopped at difficulty selection; a subsequent confirmed input continued. Controls released.

The reported loading-mask regression remains unconfirmed by a captured loading draw. The five packaged loading/passthrough shader binaries (including alpha-disabled variants) match the September 18 tester VPK byte-for-byte. This excludes changed binaries between those two packages, not runtime state, textures, or an older regression. The first trace captured a menu; the second captured gameplay frame 12590 (358 command indices ending at 357), not the loading frame. A screenshot request during the transition returned HTTP 504 (no completed display frame), after which status/log requests succeeded and frame count advanced. Do not describe this as a crash or use traced-frame time as a performance result.

Corrected tools/vita_remote.py messages: trace-draw and trace-pages now confirm their own requests; env no longer incorrectly claims it queued both traces. Syntax compilation passed. No rendering fix is claimed from this CLI correction.

Next: capture the actual loading draw at submission, then inspect mask texture, blend/write mask and sampler state. Avoid changing the shader on appearance alone. For performance, account for Claude's newer evidence: hierarchy assistance is reported too small to matter, query unlocking already exists, and full render views currently add approximately 4–5 ms without overlapping tick and scene. The proposed overlap requires scheduler and shared-write ownership work; do not assume the rough max(tick, scene) estimate is a verified result. No additional kernel clock patch is justified by the evidence gathered here.

## Perf65 loading capture

Added opt-in XV_LOADING_TRACE (default off), capped at four draws per loading shader key. Logs captured draw state plus texture formats/border colors; it does not enable vertex readback or change rendering. Full build and the production texture-resolution sanitizer fixture passed. Perf65 / 3ce508b deployed with verified/restart_requested/boot_confirmed true in slot 0. Runtime SHA-256 `6ccbb0f8afdbe456434f46ee9d404400ab035e8e6e057fde5f13a5ba5e0660d7`, 32,148,374 bytes. Only game-a.self and boot-game.txt changed from the retained perf64 package. Evidence: ../loading-state-hardware.

The remote environment is process-local, so XV_LOADING_TRACE=1 was set again on the dashboard after reboot and before game launch. Startup.log captured frames 2–5:

- C4B1822B: all four stages substitute previous framebuffer (`previous F`), UV scales 1/640 and 1/480; blend enabled SRCALPHA/ONE, write mask F.
- C61481BC: both textures captured, no previous-frame substitution; stage 0 is 320x240 linear RGBA, stage 1 is 128x16 swizzled RGBA. Format 0C000000 both. Stage 1 guest addressing 4/3 (BORDER/CLAMP), GXM 5/2 (full-border/clamp), border 00000000. Blend enabled SRCALPHA/ONE, write mask F, alpha test disabled.
- Immediate input logging around the mask pass includes stage-1 U coordinates from approximately 127.92 to 767.92 before normalization by 1/128. Out-of-range sampling is therefore relevant. The program's output alpha multiplies the stage-1 sample alpha by vertex alpha.

`after-launch.png` visually confirms the reported full-screen blue feedback effect on perf65. This is a loading screenshot, not gameplay performance evidence. Controls released. No rendering fix or FPS gain is claimed. Next isolate out-of-range mask alpha and feedback accumulation; do not assume merely binding both textures establishes correct mask contents/sampling. Capture is bounded and exhausted after startup; no perpetual trace was enabled.
