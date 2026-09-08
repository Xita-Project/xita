# Windows build guide

[README](../README.md) · [Build and install](building.md)

An [experimental native PowerShell guide](windows-powershell.md) is also available.
It uses Windows VitaSDK plus MSYS2 utilities and awaits Windows validation.

Use **WSL2 with Ubuntu** for Xita's Bash/Make workflow. Native Windows building
has not been validated, and this guide has not yet been tested end to end on
a Windows machine. The release input gaps in the [build guide](building.md)
apply here too.

## 1. Install WSL2

In an administrator PowerShell window:

```powershell
wsl --install -d Ubuntu
```

Restart if requested, open Ubuntu, and create its Linux user account. Check
the distribution from PowerShell:

```powershell
wsl --list --verbose
```

Ubuntu should show version 2. If necessary, use its exact name with
`wsl --set-version Ubuntu 2`. Microsoft's
[installation guide](https://learn.microsoft.com/en-us/windows/wsl/install)
covers supported Windows versions and virtualization troubleshooting.

## 2. Prepare Ubuntu and VitaSDK

Run these commands in **Ubuntu**, not PowerShell:

```sh
sudo apt update
sudo apt install build-essential cmake git curl wget python3 python3-venv \
  pkg-config unzip zip xz-utils
```

Follow [VitaSDK's setup instructions](https://vitasdk.org/) and its current
[bootstrap guide](https://github.com/vitasdk/vdpm#bootstrap). For a new installation:

```sh
mkdir -p "$HOME/tools"
cd "$HOME/tools"
git clone https://github.com/vitasdk/vdpm.git
cd vdpm
export VITASDK="$HOME/vitasdk"
./bootstrap-vitasdk.sh
export PATH="$VITASDK/bin:$PATH"
arm-vita-eabi-gcc --version
```

Keep the `VITASDK` and `PATH` exports in your Ubuntu shell configuration for
later sessions. Record the SDK version used; do not replace a working SDK
merely to follow the example.

## 3. Use Linux storage for the checkout

Keep your authorized checkout in a path such as `~/src/xita`, rather than
building under `/mnt/c`. This avoids Windows file-mode and filesystem overhead
issues. Public repository availability is not implied by this guide.

```sh
cd "$HOME/src/xita"
python3 -m venv .venv
. .venv/bin/activate
python -m pip install iced-x86
```

Windows drives appear under `/mnt/c`, `/mnt/d`, and so on. Copy your own game
files into `haloce/`, then follow [game preparation](building.md#prepare-your-game)
and [building](building.md#build-the-development-port).

Start with `make -j2 RECOMP=1`. Compiling translated game code can use
substantial memory. If a compiler process is killed, try `-j1` and check WSL's
memory allocation. This is the computer's build memory, not the Vita's budget.

## 4. Copy using Windows USB

USB passthrough into WSL is unnecessary for file copying. Open your Ubuntu
checkout in Windows Explorer:

```sh
explorer.exe .
```

Connect VitaShell in USB mode. In Explorer, copy the locally built VPK and game
files to the Vita drive using the [installation layout](building.md#install-over-usb).
Safely eject the drive in Windows, exit USB mode, then install with VitaShell.
Keep existing saves backed up.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| Bash or `make` command unrecognized | Run build commands in Ubuntu. |
| `VITASDK is not set` or compiler missing | Export the actual SDK path and add its `bin` directory to `PATH`. |
| `iced-x86 is required` | Activate `.venv` and install the package there. |
| `bad interpreter` with `^M` | Shell scripts need LF line endings; use a Linux checkout. |
| Missing symbol, shader or generated file | Review the development input requirements; the filtered source export is not a complete Halo build yet. |
| Vita drive absent in Ubuntu | Copy with Windows Explorer; WSL does not need to own the USB device. |
