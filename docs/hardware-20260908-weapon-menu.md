# September 8 USB installation

The combined candidate was installed at **07:00 CDT** through the existing USB
executable update workflow. No new VPK was required. Installation is verified;
there is no new hardware gameplay or FPS measurement yet.

## Included changes

- [Weapon stencil, solo-lobby and menu-audio corrections](weapon-menu-20260908.md).
- Graphics triple-buffer control, default off; offline GPL About / License page.
- [Deferred exact visibility results](flare-defer-20260907.md), enabled by default,
  with the eager/deferred/eager comparison on **L + R + Square**.
- The previously installed frame-owned constants and exact NEON byte comparison.

The installed candidate was built in an isolated, validated source stage. It
does not include unrelated experiments from the development checkout.

## Verification and preservation

Before writing, 1,655 app/data/save/cache/log/dump/screenshot files were backed up
and verified, totaling 501,002,311 bytes. The executable was compressed and padded
to the existing 30,891,526-byte allocation. Decoded SELF segments match the native
candidate; all 1,568 matching installed shader programs match the source stage.

The update overwrote only the existing executable without resizing it. Direct
device readback and a separate read-only remount both verified its SHA-256:

```
ee0d03b642c5c919a157a43296148ce8f3127ced9f6988ebccd70398e48d92bb
```

The other 1,654 backed-up files remain byte-identical. Settings and saves are
preserved, and the card is safely unmounted. Private backup and deployment
records are under `xita-backups/2026-09-08-065547-skate3-vita-optimization/`.

## Next test

Exit USB mode and launch Xita. Keep the same graphics settings and triple
buffering off for the baseline. Listen to a minute of main-menu music, enter a
solo Blood Gulch match normally, check the rifle against a wall, and use Pause /
Leave Game. Run **L + R + Square** for the controlled visibility comparison.
Record driving, firing and any death/GPU crash separately from the benchmark.

The previous sampled session averaged **11.00 FPS at 640×360** while driving,
shooting and looking around. That is the prior build's result, not evidence of
a gain from this installation. The next optimization decision will compare
visibility wait, draw preparation and total frame time at unchanged settings.

No commit, GitHub push, history rewrite or publication was performed.
