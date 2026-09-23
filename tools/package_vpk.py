#!/usr/bin/env python3
"""Package existing Xita build files without a command line per shader.

Works with native Windows Python as well as POSIX hosts. This performs no build,
installation or upload. Halo-linked packages are private development artifacts.
"""
import argparse
import hashlib
import re
from pathlib import Path
import tempfile
import zipfile


def update_contract(files):
    # Both host packager and uploader compute this over the actual package.
    # A changed launcher/shader/asset requires a complete VPK installation.
    h = hashlib.sha256()
    for name in sorted(files):
        if name in {"game-a.self", "boot-game.txt", "update-contract.txt",
                    "halo2-a.self", "boot-halo2.txt", "halo2-update-contract.txt"}:
            continue
        h.update(name.encode() + b"\0" + hashlib.sha256(files[name]).hexdigest().encode() + b"\n")
    return h.hexdigest()


def update_record(size, sha, contract):
    prefix = f"XITA1 0 {size} 1 {sha} {contract}"
    return (prefix + " " + hashlib.sha256(prefix.encode()).hexdigest() + "\n").encode()


def halo2_contract(asset_contract):
    # A valid CE transfer must never be accepted as an H2 executable update.
    return hashlib.sha256(("halo2\0" + asset_contract).encode()).hexdigest()


def halo2_payload(package_path):
    """Import only a bundling-aware H2 build, never arbitrary ZIP paths."""
    with zipfile.ZipFile(package_path) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if (len(set(names)) != len(names) or
                any(entry.file_size > 128*1024*1024 or
                    (entry.external_attr >> 16) & 0o170000 == 0o120000 for entry in entries) or
                sum(entry.file_size for entry in entries) > 256*1024*1024):
            raise ValueError("Invalid or oversized Halo 2 package")
        if not {"eboot.bin", "halo2_image.bin", "bundled-mode.txt"}.issubset(names):
            raise ValueError("Halo 2 input must be built with BUNDLED=1")
        if archive.read("bundled-mode.txt") != b"xita-halo2-bundle-v1\n":
            raise ValueError("Unsupported Halo 2 bundle format")
        runtime = archive.read("eboot.bin")
        if runtime[:4] != b"SCE\0" or not 4096 <= len(runtime) <= 128*1024*1024:
            raise ValueError("Invalid Halo 2 executable")
        result = {"halo2-a.self": runtime}
        for name in names:
            if name in {"eboot.bin", "sce_sys/param.sfo", "bundled-mode.txt"}:
                continue
            if not re.fullmatch(r"[A-Za-z0-9_.-]+\.(?:gxp|contract\.bin)", name) and name not in {"halo2_image.bin", "halo2-dsp.bin"}:
                raise ValueError(f"Unexpected Halo 2 package asset: {name}")
            result["halo2/" + name] = archive.read(name)
        return result


def package(root, eboot, sfo, output, include_scenes=False, launcher=None, halo2_package=None):
    root, eboot, sfo, output = map(Path, (root, eboot, sfo, output))
    files = {"sce_sys/param.sfo": sfo, "eboot.bin": eboot}
    for name in ("LICENSE", "NOTICE", "THIRD_PARTY.md"):
        files[name] = root / name
    for pattern in ("shaders/*.gxp", "sce_sys/*.png", "sce_sys/livearea/contents/*", "LICENSES/*.txt"):
        for source in sorted(root.glob(pattern)):
            if source.is_file():
                if source.name.endswith("_az.frag.gxp"):
                    continue   # alpha-zero programs are embedded in the executable; the package (and executable-only updates) stay unchanged
                if source.relative_to(root).as_posix().startswith("sce_sys/") and source.suffix not in {".png", ".xml", ".jpg", ".jpeg"}:
                    raise ValueError(f"Unexpected LiveArea asset: {source.name}")
                files[source.relative_to(root).as_posix()] = source
    if include_scenes:
        for source in sorted(root.glob("assets/*.bin")):
            files[source.relative_to(root).as_posix()] = source
    if launcher:
        files["eboot.bin"] = Path(launcher)
        files["game-a.self"] = eboot
    for name, source in files.items():
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"Missing or symlinked package input: {name}")
        if source.resolve() == output.resolve():
            raise ValueError("Output must not replace a package input")
    if eboot.read_bytes()[:4] != b"SCE\0" or sfo.read_bytes()[:4] != b"\0PSF":
        raise ValueError("Expected a signed Vita SELF and param.sfo")
    if halo2_package and not launcher:
        raise ValueError("A combined package requires the shared launcher")
    generated = halo2_payload(halo2_package) if halo2_package else {}
    if launcher:
        if Path(launcher).read_bytes()[:4] != b"SCE\0" or not 4096 <= eboot.stat().st_size <= 64*1024*1024:
            raise ValueError("Invalid updater launcher or game executable size")
        abi = update_contract({**{name: source.read_bytes() for name, source in files.items()}, **generated})
        generated["update-contract.txt"] = (abi + "\n").encode()
        generated["boot-game.txt"] = update_record(eboot.stat().st_size, hashlib.sha256(eboot.read_bytes()).hexdigest(), abi)
        if halo2_package:
            h2abi = halo2_contract(abi)
            runtime = generated["halo2-a.self"]
            generated["halo2-update-contract.txt"] = (h2abi + "\n").encode()
            generated["boot-halo2.txt"] = update_record(len(runtime), hashlib.sha256(runtime).hexdigest(), h2abi)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Finish and validate the complete ZIP before replacing a prior VPK.
    with tempfile.TemporaryDirectory(prefix=".xita-vpk-", dir=output.parent) as temp:
        staged = Path(temp) / "package.vpk"
        with zipfile.ZipFile(staged, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, source in files.items():
                archive.write(source, name)
            for name, data in generated.items():
                archive.writestr(name, data)
        with zipfile.ZipFile(staged) as archive:
            if archive.testzip() is not None:
                raise ValueError("Package CRC validation failed")
        staged.replace(output)
    return {"files": len(files) + len(generated), "bytes": output.stat().st_size,
            "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--eboot", type=Path)
    parser.add_argument("--sfo", type=Path)
    parser.add_argument("--launcher", type=Path, help="stable updater SELF; game runtime becomes game-a.self")
    parser.add_argument("--halo2-package", type=Path, help="private Halo 2 VPK built with BUNDLED=1; adds the second game to this install")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--include-scenes", action="store_true", help="include assets/*.bin for the mock build only")
    args = parser.parse_args()
    try:
        result = package(args.root, args.eboot or args.root / "build/eboot.bin",
                         args.sfo or args.root / "build/param.sfo", args.output, args.include_scenes, args.launcher, args.halo2_package)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"{args.output}: {result['files']} files, {result['bytes']} bytes")
    print(f"SHA256 {result['sha256']}")


if __name__ == "__main__":
    main()
