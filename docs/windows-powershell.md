# Experimental Windows / PowerShell build

[README](../README.md) · [WSL2 guide](windows.md) · [Build inputs](building.md)

This is a **native Windows experiment driven from PowerShell 7**. It uses the
Windows VitaSDK compiler and MSYS2's Make/Bash utilities. It does not require
WSL, but it is not a PowerShell-only replacement for the existing Makefiles.
**No end-to-end Windows build has been verified yet.** WSL2 remains the established
project workflow. Both paths need the same private game and shader inputs.

VitaSDK now documents a native mingw toolchain and `bootstrap-vitasdk.ps1`, while
recommending WSL2. This guide follows that supported bootstrap entry point rather
than selecting a Linux archive or an old Windows nightly.
[VitaSDK setup](https://vitasdk.org/#getting-started),
[PowerShell bootstrap source](https://github.com/vitasdk/vdpm/blob/c83b88a54ec13372515eeb8b3bdca4b10c36d721/bootstrap-vitasdk.ps1).

## Tools

Use Windows 10/11 x64, PowerShell 7, Git for Windows, native Windows Python 3.10+
with the `py` launcher, and [MSYS2](https://www.msys2.org/). Install these from
their official distributions. Use short paths **without spaces**, such as
`C:/dev/xita`, `C:/dev/vdpm`, and `C:/vitasdk`; the current Makefiles contain
unquoted POSIX paths. Native Windows ARM64 has not been evaluated.

In the MSYS2 terminal, finish its normal updates and install Make and the shell
utilities used by Xita. If the update asks you to close/reopen the terminal, do
that before the second command:

```sh
pacman -Syu
pacman -S --needed make bash coreutils gawk
```

These utilities support the recipes; VitaSDK supplies the ARM compiler.
Do not replace it with a MinGW compiler targeting Windows.
[MSYS2 environments](https://www.msys2.org/docs/environments/).

## Install the Windows VitaSDK

In PowerShell 7, choose an SDK destination that does not already exist. The
upstream bootstrap refuses to overwrite an existing installation. Keep a known
working SDK rather than replacing it to follow an example.

```powershell
git clone https://github.com/vitasdk/vdpm.git C:/dev/vdpm
if ($LASTEXITCODE -ne 0) { throw 'vdpm clone failed' }

# Review the upstream script before running it.
Get-Content C:/dev/vdpm/bootstrap-vitasdk.ps1
pwsh -NoProfile -File C:/dev/vdpm/bootstrap-vitasdk.ps1 -InstallDirectory C:/vitasdk
if ($LASTEXITCODE -ne 0) { throw 'VitaSDK bootstrap failed' }

$env:VITASDK = 'C:/vitasdk'
$env:PATH = "$env:VITASDK/bin;C:/msys64/usr/bin;$env:PATH"
arm-vita-eabi-gcc --version
vdpm status
```

Record the reported SDK version/channel with results. These environment changes
apply to the current terminal. The bootstrap and Xita have different dependency
requirements: installing VitaSDK does not remove Xita's need for Bash/Make.

## Prepare the checkout and game inputs

Use your authorized private checkout. In PowerShell:

```powershell
Set-Location C:/dev/xita
py -3 -m venv .venv
if ($LASTEXITCODE -ne 0) { throw 'Python environment creation failed' }
$XitaPython = 'C:/dev/xita/.venv/Scripts/python.exe'
& $XitaPython -m pip install iced-x86
if ($LASTEXITCODE -ne 0) { throw 'iced-x86 installation failed' }

& $XitaPython xita_recomp.py --list-profiles
```

Copy your supported `default.xbe` and maps into `haloce/`, with the matching
local symbol file and shader inputs described in [the build guide](building.md).
The experimental Windows path does not solve the remaining source-release
input/provenance gaps.

Generate metadata as UTF-8 **without a BOM**. Windows PowerShell 5.1's usual
redirection encoding is unsuitable for this JSON consumer; use PowerShell 7
and the explicit encoding below:

```powershell
$XitaManifest = & $XitaPython xbe_parse.py haloce/default.xbe --json
if ($LASTEXITCODE -ne 0) { throw 'XBE parsing failed' }
$XitaManifest | Set-Content -Encoding utf8NoBOM game_manifest.json

& $XitaPython xita_recomp.py haloce/default.xbe --profile halo_ce_3925 --symbols halo_symbols.json --check-profile
if ($LASTEXITCODE -ne 0) { throw 'Game profile validation failed' }

& $XitaPython xita_recomp.py haloce/default.xbe --profile halo_ce_3925 --symbols halo_symbols.json -o recomp
if ($LASTEXITCODE -ne 0) { throw 'Recompilation failed' }
& $XitaPython tools/gen_native_clip.py
if ($LASTEXITCODE -ne 0) { throw 'Native helper generation failed' }
& $XitaPython xbe_image.py haloce/default.xbe game_manifest.json recomp/halo_image.bin
if ($LASTEXITCODE -ne 0) { throw 'Game image generation failed' }
```

The profile applies the Halo lobby patch automatically. No separate Bash
`tools/recomp.sh` invocation is necessary in PowerShell.

## Compile and package

Use MSYS2 Make with an explicit Bash shell and native Python. Start with two
compiler jobs because generated functions use substantial build memory:

```powershell
$XitaBuildArgs = @(
    '-j2', 'RECOMP=1',
    'SHELL=C:/msys64/usr/bin/bash.exe',
    "PYTHON=$XitaPython",
    'build/eboot.bin', 'build/param.sfo'
)
& C:/msys64/usr/bin/make.exe @XitaBuildArgs
if ($LASTEXITCODE -ne 0) { throw 'Native Vita build failed; keep the full build log' }

& $XitaPython tools/package_vpk.py --output xita.vpk
if ($LASTEXITCODE -ne 0) { throw 'VPK packaging failed' }
Get-FileHash .\xita.vpk -Algorithm SHA256
```

Python packaging gathers the existing compiled shaders internally, avoiding a
very long `vita-pack-vpk -a ...` command on Windows. It checks required inputs,
Vita file signatures, and ZIP integrity, and includes software notices. It does
not compile missing shaders or add your maps, saves, SDK module, or flattened
Halo image. Those game files use the separate installation layout.

Compile changed shaders through the documented local/on-device workflow before
packaging. Do not treat an older `.gxp` file as an automatically rebuilt shader.

Copy the VPK and required game data through Windows Explorer with VitaShell in
USB mode; [back up saves and follow the installation layout](building.md#install-over-usb).
Safely eject before leaving USB mode. GitHub release packages remain private
development artifacts while the repositories are private.

## Report the first Windows validation

Include Windows and PowerShell versions, `vdpm status`, the compiler version,
Git commit, Python version, and the failing command/log excerpt. Useful checks:

| Symptom | Next check |
| --- | --- |
| `python3` opens the Store or is missing | Pass the explicit `PYTHON=.../python.exe` Make argument. |
| `command`, `awk`, `mkdir`, or `rm` missing | Check MSYS2 utilities, the explicit Bash shell, and PATH. |
| Compiler is a Linux executable or cannot launch | Use the native Windows SDK bootstrap. |
| Profile digest mismatch | Use the exact supported XBE and matching symbols. |
| JSON encoding error | Recreate metadata with PowerShell 7 and `utf8NoBOM`. |
| Command line too long while packing | Build `eboot.bin`/`param.sfo`, then use `tools/package_vpk.py`. |
| Missing `.cg`, `.gxp`, layout, or symbol files | Resolve the same development-input gaps as Linux; changing shells cannot fix them. |
| Path conversion or SDK import-library failure | Retain the exact error and SDK identity; this path remains experimental. Use WSL2 to continue. |

Local validation covers profile generation, package contents/integrity, and a
Linux-hosted VitaSDK link. It does not establish native Windows compatibility
or installation of the newly assembled VPK on a physical Vita.
