# Original copy alignment paths

Native198 reaches original CRT copy routine `320890` during the menu text-token replacement. Its forward path jumps at `3208DD` through `[3208F0 + EAX*4]`; EAX=2 selects original tail `32092C`. The destination alignment test and following `AND EAX,3` prove indices 1..3. The backward overlap path has the equivalent guarded table at `320A7C`. In both cases, index0 overlaps adjacent instruction bytes and is unreachable through the proven branch. Treating those bytes as the first ordinary table entry prevented automatic discovery.

The H2-only preparation step now follows just the six valid alignment tails: `320900`, `32092C`, `320950`, `320A8C`, `320AB0` and `320AD8`. Four exact fingerprints cover both selecting instruction spans and both three-entry data spans. Existing generic indirect-tail dispatch reads the real guest table without adding a return address. The original copy instructions, direction handling, saved registers and epilogue execute unchanged. No memory-copy HLE, generic discovery change or CE edit is involved.

All 52 callback-root tests pass, including exact slot exclusion, invalid targets and revision guards. Private execution validation compares the owned x86 routine under Unicorn with its ordinary lifted C and with an independent overlap-safe byte-copy oracle. All 13,248 cases pass: all source/destination alignments, non-overlapping buffers, overlapping buffers in both directions, equal pointers, lengths 0..131 and selected boundaries through 2049. The comparison checks all eight general registers including ESP, the direction flag and the complete 64KiB arena including the original saved stack words. Arithmetic condition flags are volatile at this CRT call boundary and are not part of that ABI comparison.

The reproducible oracle tool requires the owned revision and creates its output outside the checkout:

```sh
python tools/verify_halo2_move_alignment.py /private/owned/default.xbe \
  --out /private/new-copy-oracle
```

It requires iced_x86, Unicorn and a host C compiler. Its generated source, executable bytes and test images remain private; none belong in Git. Existing 12,608 function bodies are unchanged by full regeneration; only six original tails are added, and the unsupported-instruction count stays at 3663. These translation counts are not compatibility evidence.

Native199 passes the copy alignment path and executes two additional original packed-color rectangles at END sources `03B803A0` and `03B806C4` (eight completed rectangles total). It then accepts BEGIN `03B80988` but rejects END `03B809E4`: the next original fullscreen rectangle uses four zero UV pairs instead of the supported full-texture UV corners. Its texture is a completely captured 4×4 BC2 allocation at `018FAF80`, and every original packed vertex color is opaque black. The next task is to validate constant-UV sampling and admit that original geometry; no fade or presentation is skipped.

The original Microsoft intro is visually confirmed. The terminal desktop capture is already back at the Vita3K library after the diagnostic exit. The complete last-presented game image is still black frame135: **the original main menu remains absent**. GET stops at `03B809E8`, PUT at `03B80AF4`. The owned :111 emulator was stopped after capture.

Private evidence is in `native-199-artifacts`, `native-199-view`, `native-milestone-199.json` and `move-alignment`. The four-job build verifies all 180 dependencies and 128 generated source paths. Graphics/dispatcher/sprite/audio objects and the owned image remain byte-identical to native198. ELF SHA-256 is `ee43de84e907e5613f5a9e82e803e7542281446c8f5890bf5f86166d84e25cc3`; EBOOT is `cd82b500d8fc54705a502b1ff70be8d3d901b92f8b63b7d7432071c11314cec4`. Trace SHA-256 is `dcdba1d7945ecc2c925c6f447f30ab2800274493a450e6fa70dc381ef2459cf7`, channel snapshot `24a59d83fa8b9a38c9cfc59caaf5725b8df800677543c67378361a7809d54a18`, last-presented image `20547a64d5e503077a501b87032cc2762482a781f89a3add537e31e4d2ba6893`.

Exact private replay with the owned emulator stopped:

```sh
python3 preserve_fresh_cache.py native199-replay
python3 capture_run.py 199-replay native-199-artifacts
python3 drive_startup.py 199-replay native-199-artifacts
```

The diagnostic package embeds owned game code/image and must not be distributed as a release.
