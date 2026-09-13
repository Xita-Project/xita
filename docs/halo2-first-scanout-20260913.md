# Halo 2: the initial framebuffer reaches the Vita display

Native attempt 31 presents the guest's initial 640-by-480 framebuffer through
`sceDisplaySetFrameBuf` and a real Vita vblank. **Its contents are entirely black.
There is no title screen, menu or drawn color content.** The actual emulator
capture and a complete raw scanout snapshot establish this limitation.

## Supported presentation contract

The adapter accepts only the observed step-zero mode `88070701`, format `12`,
pitch 2,560, after the validated initialization flip and mode-transition vblank.
It verifies the full framebuffer mapping and canonical color region before any
host presentation. Wrong modes/formats, unmapped final pages and failed host
calls stop. Success preserves CPU state except the export's EAX result and
RET-24 stack effect.

The converter copies little-endian Xbox A8R8G8B8 pixels to Vita A8B8G8R8,
applies the captured RGB gamma tables and makes scanout alpha opaque. The image
is centered at its native 640-by-480 size in a 960-by-544 framebuffer. Borders
are black. Complete source/output/LUT spans and output aliasing are checked
before writes. Two CDRAM display buffers avoid modifying the active image.

The primary [AV mode table](https://github.com/Cxbx-Reloaded/Cxbx-Reloaded/blob/585c49a50af1255ab155099e06f24505f9c5a800/src/core/kernel/exports/EmuKrnlAvModes.h)
identifies this as progressive 480p. [NKPatcher's configuration source](https://github.com/Rocky5/Xbox-Softmodding-Tool/blob/53203aa1c1272468c9374f260233f92557c9f324/App%20Sources/NKPatcher/Main%20NKP11/config.inc)
explicitly describes flicker filtering as interlaced-only. The restricted digital
480p presenter therefore treats the recorded flicker setting as inactive; it
requires the observed disabled luma-filter setting. This is a progressive digital
scanout contract, not a simulation of analog encoder output. Interlaced modes
and enabled luma filtering remain unsupported.

## Observed result

The real mode-transition wait advances Vita vcount 31 to 32. Mode application
then presents physical guest framebuffer `039D0000` at vcount 33. A complete
scanout snapshot contains exactly 522,240 opaque black pixels, including the
border. No guest color draw or clear has populated this initial buffer.

The next stop is `AvSendTVEncoderOption` 15 (field-status query), parameter zero,
output `005E5F9C`, return `3F9C51`. The adapter still rejects that query. The
diagnostic stop retains the actual last presented buffer for three seconds to
permit capture; guest execution does not continue during that delay.

| Attempt 31 artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ELF | 65,352,480 | `c28dd0045805a74d7f3ac4940434bd94fa74eef2d69494c53f8885c339eac729` |
| EBOOT | 80,089,522 | `7b80b85b9f6de8cb94e057a085337ce08e97eebc04a6845fe69def740fb6d1d3` |
| VPK | 22,802,512 | `9defb38ae89a364ecb6fb5df5572155bbd55b1528cf51f3bdad616ca6dcd904b` |
| Boot log | 196,011 | `1d1dea0a276335bb9d632bec22b6318d8d5fbc629913e36bbe27e266bcb14af8` |
| Raw scanout | 2,088,976 | `a64f22fb7fb7c4dd5be03767147ce06237872341efc10fd3252262f7fc4606d1` |

The private screenshot is `private/native-31-view/window-4.0.png`, with its video,
full hash manifest and raw snapshot in the same directory. The raw header is
four little-endian words: width, height, pitch in bytes, presented-frame count.
Earlier captures at 0.8 and 1.5 seconds precede emulator-window creation and must
not be used as evidence of the presented frame. Build/log/device/push artifacts
are separately archived in `private/native-31-artifacts/`. The emulator is stopped.

All thirteen host executables pass. The converter checks every pixel in a
synthetic image with distinct per-channel gamma, alpha and border expectations;
short/overlapping/wrapping spans preserve destination bytes. Runtime tests cover
full mappings, host failure and ABI preservation. Converter/runtime ASan/UBSan
also pass. Shared Halo CE paths are unchanged.

Next is the progressive field/blanking contract, then the first actual color
rendering commands. Rendering and a usable Halo 2 menu remain outstanding.
Use the [isolated build procedure](halo2-host-channel-20260913.md). Diagnostic
VPKs embed owned game image/code and must not be distributed or uploaded as releases.
