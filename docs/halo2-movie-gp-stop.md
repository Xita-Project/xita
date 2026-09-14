# Halo 2: drain retained movie grains before Stop

Native141 completes the original loaded-DSP movie Stop, rewind and restart
sequence. It executes 139 real Stops and 137 stopped zero rewinds. The actual
Start input later completes movie cleanup, but selects the title-exit path:
`21347F -> 2238F4 -> 22376B -> 3F8AD0`, then the checked display-mode write at
`3F8BE8`, address `0327B040`, mode `88070701`. Its 381 presented frames end
RGB-black; the captured window does not show a main menu.

The real GP worker may retain one computed movie grain in addition to the
submitted sink grain. Stop now halts the actual decoder and marks that voice
as draining under the progress mutex. The worker submits the already computed
grain, while subsequent computations exclude that stopped movie voice and
continue the existing FX histories. Stop returns only after both retained and
submitted movie ownership retire through the real sink. Rewind and release
remain rejected until that point. A two-second drain timeout is an explicit
backend failure; it does not report completion or discard retained resources.
Other playing voices still pass the existing exact inventory check.

All 44 host executables pass. The concurrent real-decoder/GP test holds both
ownership stages, verifies that Stop cannot finish and rewind/forget reject,
then releases the sink and checks stopped cursors, repeated Stop and restart.
That test passes ASan/UBSan and ThreadSanitizer. The original Stop ABI and
failure guards remain covered by the existing buffer tests. All 170 native
dependency targets were checked before launch.

This run reused a populated private cache4. Its raw-access rejection prevents
N: mounting and reproduces the previously documented map-load failure/Start
exit. It must not be read as evidence that menu initialization fails after a
successful cache mount. Native142 therefore replays the identical build with
an empty private cache4 after preserving and hashing the prior directory and
raw image. The original formatter supplies the new metadata. This controlled
fresh-volume experiment does not implement general FATX coherence.

| Artifact | Native141 SHA-256 |
|---|---|
| ELF | `732a7ef4ada15a74a7acce06625b691b9725d1d915a1beba8dfb35f1d6e036bb` |
| EBOOT | `8957f99b3e2fdefeebc889e7c5cd5bd916ced24565b0381e5d371ec7c07846ab` |
| VPK | `364a481e08f9f6b77ffe11ccda9aab9d7750cdb5fd1a5ad68139539772d66390` |
| Guest trace | `884d6d984ec494bf4f698c740bbd7d7af8dd2df64fdb7266331a4497a73269a2` |
| Final black frame381 | `86f23fdad0ce189b755a97e94f0b29fdb757ebd9e005926fd5c043014b426fc5` |

Private evidence is in `native-141-artifacts`, `native-141-view`,
`audio-movie-stop-gp/native-build-identity.json`, `dsp-bringup/movie-stop-gp-*`,
and `movie-skip-audit/native141-late-input.json`. Enter/Start was pressed at
21:55:28.904 UTC and released two seconds later, with guest `digital=0010`.
The owned native141 process has exited. From the private directory:

```sh
python3 run_lab.py 141-replay native-141-artifacts/halo2-boot.vpk
python3 run_lab.py stop
```

Build uses the existing `audio-stream-process` generated source/image and
prior diagnostic flags, output `audio-movie-stop-gp/build`. The package embeds
owned game code/data and must not be uploaded or distributed. No claim of
physical speaker audibility follows from the muted SDL-dummy emulator sink.
