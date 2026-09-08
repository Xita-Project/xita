#!/usr/bin/env python3
"""Package existing Xita build files without a command line per shader.

Works with native Windows Python as well as POSIX hosts. This performs no build,
installation or upload. Halo-linked packages are private development artifacts.
"""
import argparse
import hashlib
from pathlib import Path
import tempfile
import zipfile


def package(root, eboot, sfo, output, include_scenes=False):
    root, eboot, sfo, output = map(Path, (root, eboot, sfo, output))
    files = {"sce_sys/param.sfo": sfo, "eboot.bin": eboot}
    for name in ("LICENSE", "NOTICE", "THIRD_PARTY.md"):
        files[name] = root / name
    for pattern in ("shaders/*.gxp", "sce_sys/*.png", "sce_sys/livearea/contents/*", "LICENSES/*.txt"):
        for source in sorted(root.glob(pattern)):
            if source.is_file():
                if source.relative_to(root).as_posix().startswith("sce_sys/") and source.suffix not in {".png", ".xml", ".jpg", ".jpeg"}:
                    raise ValueError(f"Unexpected LiveArea asset: {source.name}")
                files[source.relative_to(root).as_posix()] = source
    if include_scenes:
        for source in sorted(root.glob("assets/*.bin")):
            files[source.relative_to(root).as_posix()] = source
    for name, source in files.items():
        if source.is_symlink() or not source.is_file():
            raise ValueError(f"Missing or symlinked package input: {name}")
        if source.resolve() == output.resolve():
            raise ValueError("Output must not replace a package input")
    if eboot.read_bytes()[:4] != b"SCE\0" or sfo.read_bytes()[:4] != b"\0PSF":
        raise ValueError("Expected a signed Vita SELF and param.sfo")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Finish and validate the complete ZIP before replacing a prior VPK.
    with tempfile.TemporaryDirectory(prefix=".xita-vpk-", dir=output.parent) as temp:
        staged = Path(temp) / "package.vpk"
        with zipfile.ZipFile(staged, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, source in files.items():
                archive.write(source, name)
        with zipfile.ZipFile(staged) as archive:
            if archive.testzip() is not None:
                raise ValueError("Package CRC validation failed")
        staged.replace(output)
    return {"files": len(files), "bytes": output.stat().st_size,
            "sha256": hashlib.sha256(output.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--eboot", type=Path)
    parser.add_argument("--sfo", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--include-scenes", action="store_true", help="include assets/*.bin for the mock build only")
    args = parser.parse_args()
    try:
        result = package(args.root, args.eboot or args.root / "build/eboot.bin",
                         args.sfo or args.root / "build/param.sfo", args.output, args.include_scenes)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"{args.output}: {result['files']} files, {result['bytes']} bytes")
    print(f"SHA256 {result['sha256']}")


if __name__ == "__main__":
    main()
