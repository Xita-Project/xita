#!/usr/bin/env python3
"""
xbe_parse.py — Stage 1 of the Xita offline static-recompilation pipeline.

Parses an original Xbox executable (.xbe) image header using only the Python
standard library and reports:

  * magic verification ('XBEH')
  * BaseAddress / SizeOfHeaders / SizeOfImage / AddressOfEntryPoint
    (entry point unmasked with the retail kernel XOR key 0xA8FC57AB)
  * every section header (name, VA, VSize, raw offset, raw size, flags)
  * every linked XDK library version (name, major.minor.build, QFE, flags)

Also surfaced because later pipeline stages need them and they live in the
same header: the unmasked kernel-thunk address, the TLS directory address and
the certificate's TitleId / TitleName (used to derive the on-Vita save path).

Usage:
    xbe_parse.py GAME.xbe            human-readable report
    xbe_parse.py GAME.xbe --json     machine-readable JSON for Stage 2
    xbe_parse.py GAME.xbe --key debug   unmask with the debug-kernel keys instead

Exit codes: 0 success, 1 malformed/unsupported image, 2 usage / IO error.
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from typing import List, Optional

# --------------------------------------------------------------------------
# Constants
# --------------------------------------------------------------------------

XBE_MAGIC = b"XBEH"

# Kernel XOR keys.  The header's EntryPoint and KernelImageThunkAddress fields
# are stored XORed so an image can only be started by a matching kernel.
XOR_KEYS = {
    "retail":  {"entry": 0xA8FC57AB, "thunk": 0x5B6D40B6},
    "debug":   {"entry": 0x94859D4B, "thunk": 0xEFB1F152},
    "chihiro": {"entry": 0x40B5C16E, "thunk": 0x2290059D},
}

# Fixed image header (offsets from start of file).  Little-endian throughout.
#   0x000  4    Magic 'XBEH'
#   0x004  256  DigitalSignature
#   0x104  4    BaseAddress
#   0x108  4    SizeOfHeaders
#   0x10C  4    SizeOfImage
#   0x110  4    SizeOfImageHeader
#   0x114  4    TimeDate
#   0x118  4    CertificateAddress
#   0x11C  4    NumberOfSections
#   0x120  4    SectionHeadersAddress
#   0x124  4    InitFlags
#   0x128  4    AddressOfEntryPoint (XORed)
#   0x12C  4    TlsAddress
#   0x130  4    PeStackCommit
#   0x134  4    PeHeapReserve
#   0x138  4    PeHeapCommit
#   0x13C  4    PeBaseAddress
#   0x140  4    PeSizeOfImage
#   0x144  4    PeChecksum
#   0x148  4    PeTimeDate
#   0x14C  4    DebugPathnameAddress
#   0x150  4    DebugFilenameAddress
#   0x154  4    DebugUnicodeFilenameAddress
#   0x158  4    KernelImageThunkAddress (XORed)
#   0x15C  4    NonKernelImportDirectoryAddress
#   0x160  4    NumberOfLibraryVersions
#   0x164  4    LibraryVersionsAddress
#   0x168  4    KernelLibraryVersionAddress
#   0x16C  4    XapiLibraryVersionAddress
#   0x170  4    LogoBitmapAddress
#   0x174  4    LogoBitmapSize
IMAGE_HEADER_FMT = "<4s256s" + "I" * 29
IMAGE_HEADER_SIZE = struct.calcsize(IMAGE_HEADER_FMT)          # 0x178
IMAGE_HEADER_FIELDS = (
    "Magic", "DigitalSignature",
    "BaseAddress", "SizeOfHeaders", "SizeOfImage", "SizeOfImageHeader",
    "TimeDate", "CertificateAddress", "NumberOfSections",
    "SectionHeadersAddress", "InitFlags", "AddressOfEntryPoint", "TlsAddress",
    "PeStackCommit", "PeHeapReserve", "PeHeapCommit", "PeBaseAddress",
    "PeSizeOfImage", "PeChecksum", "PeTimeDate",
    "DebugPathnameAddress", "DebugFilenameAddress", "DebugUnicodeFilenameAddress",
    "KernelImageThunkAddress", "NonKernelImportDirectoryAddress",
    "NumberOfLibraryVersions", "LibraryVersionsAddress",
    "KernelLibraryVersionAddress", "XapiLibraryVersionAddress",
    "LogoBitmapAddress", "LogoBitmapSize",
)
assert len(IMAGE_HEADER_FIELDS) == 2 + 29

# Section header (0x38 bytes)
#   0x00 4  Flags
#   0x04 4  VirtualAddress
#   0x08 4  VirtualSize
#   0x0C 4  RawAddress          (file offset)
#   0x10 4  RawSize
#   0x14 4  SectionNameAddress  (VA of NUL-terminated ASCII string)
#   0x18 4  SectionNameRefCount
#   0x1C 4  HeadSharedPageRefCountAddress
#   0x20 4  TailSharedPageRefCountAddress
#   0x24 20 SectionDigest (SHA-1)
SECTION_HEADER_FMT = "<9I20s"
SECTION_HEADER_SIZE = struct.calcsize(SECTION_HEADER_FMT)      # 0x38

SECTION_FLAGS = (
    (0x00000001, "WRITABLE"),
    (0x00000002, "PRELOAD"),
    (0x00000004, "EXECUTABLE"),
    (0x00000008, "INSERTED_FILE"),
    (0x00000010, "HEAD_PAGE_RO"),
    (0x00000020, "TAIL_PAGE_RO"),
)

# Library version (0x10 bytes)
#   0x00 8  LibraryName (char[8], space/NUL padded, NOT guaranteed NUL-terminated)
#   0x08 2  MajorVersion
#   0x0A 2  MinorVersion
#   0x0C 2  BuildVersion
#   0x0E 2  Flags: [12:0] QFEVersion, [14:13] Approved, [15] DebugBuild
LIBRARY_VERSION_FMT = "<8sHHHH"
LIBRARY_VERSION_SIZE = struct.calcsize(LIBRARY_VERSION_FMT)    # 0x10

LIB_APPROVED = {0: "unapproved", 1: "possibly-approved", 2: "approved", 3: "reserved"}

INIT_FLAGS = (
    (0x00000001, "MOUNT_UTILITY_DRIVE"),
    (0x00000002, "FORMAT_UTILITY_DRIVE"),
    (0x00000004, "LIMIT_64MB"),
    (0x00000008, "DONT_SETUP_HARDDISK"),
)

# Certificate — only the leading fields we care about (0x00..0xAC).
#   0x00 4   Size
#   0x04 4   TimeDate
#   0x08 4   TitleId
#   0x0C 80  TitleName (UTF-16LE, 40 wchars)
#   0x5C 64  AlternateTitleIds[16]
#   0x9C 4   AllowedMedia
#   0xA0 4   GameRegion
#   0xA4 4   GameRatings
#   0xA8 4   DiskNumber
#   0xAC 4   Version
CERT_HEAD_FMT = "<III80s64sIIIII"
CERT_HEAD_SIZE = struct.calcsize(CERT_HEAD_FMT)                # 0xB0

GAME_REGION = (
    (0x00000001, "NA"),
    (0x00000002, "JAPAN"),
    (0x00000004, "RESTOFWORLD"),
    (0x80000000, "MANUFACTURING"),
)


# --------------------------------------------------------------------------
# Errors
# --------------------------------------------------------------------------

class XbeError(Exception):
    """Raised for any structural problem with the image."""


# --------------------------------------------------------------------------
# Data model
# --------------------------------------------------------------------------

@dataclass
class Section:
    index: int
    name: str
    flags: int
    flag_names: List[str]
    virtual_address: int
    virtual_size: int
    raw_address: int
    raw_size: int
    name_address: int
    digest: str
    raw_in_bounds: bool


@dataclass
class LibraryVersion:
    index: int
    name: str
    major: int
    minor: int
    build: int
    qfe: int
    approved: str
    debug_build: bool
    version_string: str


@dataclass
class Certificate:
    address: int
    size: int
    time_date: int
    title_id: int
    title_id_hex: str
    title_name: str
    allowed_media: int
    game_region: int
    game_region_names: List[str]
    game_ratings: int
    disk_number: int
    version: int


@dataclass
class XbeInfo:
    path: str
    file_size: int
    magic_ok: bool
    key_used: str
    base_address: int
    size_of_headers: int
    size_of_image: int
    size_of_image_header: int
    time_date: int
    time_date_iso: str
    entry_point_masked: int
    entry_point: int
    entry_point_in_image: bool
    entry_point_key_guess: Optional[str]
    kernel_thunk_masked: int
    kernel_thunk: int
    tls_address: int
    init_flags: int
    init_flag_names: List[str]
    pe_base_address: int
    pe_size_of_image: int
    debug_pathname: str
    number_of_sections: int
    section_headers_address: int
    number_of_library_versions: int
    library_versions_address: int
    kernel_library_version_address: int
    xapi_library_version_address: int
    certificate: Optional[Certificate]
    sections: List[Section] = field(default_factory=list)
    library_versions: List[LibraryVersion] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)


# --------------------------------------------------------------------------
# Parser
# --------------------------------------------------------------------------

class XbeParser:
    def __init__(self, data: bytes, path: str, key: str = "retail"):
        self.data = data
        self.path = path
        self.key = key
        self.warnings: List[str] = []
        self.sections: List[Section] = []

    # -- helpers -----------------------------------------------------------

    def warn(self, msg: str) -> None:
        self.warnings.append(msg)

    def _need(self, offset: int, length: int, what: str) -> None:
        if offset < 0 or length < 0 or offset + length > len(self.data):
            raise XbeError(
                f"{what}: needs file bytes 0x{offset:X}..0x{offset + length:X} "
                f"but file is only 0x{len(self.data):X} bytes"
            )

    def va_to_offset(self, va: int, length: int = 1) -> Optional[int]:
        """
        Translate a virtual address to a file offset.

        The header region [BaseAddress, BaseAddress+SizeOfHeaders) maps 1:1
        onto file offset 0; everything else must fall inside a section's raw
        data.  Returns None when the VA is not backed by file bytes.
        """
        base = self.base_address
        if base <= va and va + length <= base + self.size_of_headers:
            return va - base
        for s in self.sections:
            if s.virtual_address <= va and va + length <= s.virtual_address + s.raw_size:
                return s.raw_address + (va - s.virtual_address)
        return None

    def read_cstring(self, va: int, max_len: int = 256, what: str = "string") -> str:
        off = self.va_to_offset(va)
        if off is None:
            self.warn(f"{what}: VA 0x{va:08X} is not backed by file data")
            return f"<unmapped:0x{va:08X}>"
        end = self.data.find(b"\x00", off, off + max_len)
        raw = self.data[off:end if end != -1 else off + max_len]
        return raw.decode("ascii", errors="replace")

    @staticmethod
    def _flag_names(value: int, table) -> List[str]:
        return [name for bit, name in table if value & bit]

    @staticmethod
    def _iso(ts: int) -> str:
        try:
            return datetime.fromtimestamp(ts, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
        except (OverflowError, OSError, ValueError):
            return "<invalid>"

    # -- main entry --------------------------------------------------------

    def parse(self) -> XbeInfo:
        # 1. Magic -------------------------------------------------------------
        if len(self.data) < 4:
            raise XbeError("file is shorter than 4 bytes; not an XBE")
        magic = self.data[:4]
        if magic != XBE_MAGIC:
            raise XbeError(
                f"bad magic: expected {XBE_MAGIC!r}, got {magic!r} "
                f"({magic.hex()}); not an XBE image"
            )

        # 2. Fixed image header -------------------------------------------------
        self._need(0, IMAGE_HEADER_SIZE, "image header")
        hdr = dict(zip(IMAGE_HEADER_FIELDS, struct.unpack_from(IMAGE_HEADER_FMT, self.data, 0)))

        self.base_address = hdr["BaseAddress"]
        self.size_of_headers = hdr["SizeOfHeaders"]
        self.size_of_image = hdr["SizeOfImage"]

        if self.size_of_headers < IMAGE_HEADER_SIZE:
            self.warn(f"SizeOfHeaders (0x{self.size_of_headers:X}) is smaller than the fixed header "
                      f"(0x{IMAGE_HEADER_SIZE:X}); header-relative VA translation may fail")
        if self.size_of_headers > len(self.data):
            self.warn(f"SizeOfHeaders (0x{self.size_of_headers:X}) exceeds file size "
                      f"(0x{len(self.data):X}); file is truncated")
        if hdr["SizeOfImageHeader"] not in (0x178, 0x17C, 0x180, 0x184):
            self.warn(f"unusual SizeOfImageHeader 0x{hdr['SizeOfImageHeader']:X} "
                      f"(expected 0x178/0x17C/0x180/0x184)")

        keys = XOR_KEYS[self.key]
        entry_masked = hdr["AddressOfEntryPoint"]
        entry = entry_masked ^ keys["entry"]
        thunk_masked = hdr["KernelImageThunkAddress"]
        thunk = thunk_masked ^ keys["thunk"]

        img_lo, img_hi = self.base_address, self.base_address + self.size_of_image
        entry_in_image = img_lo <= entry < img_hi

        # Sanity: which key yields an entry point inside the image?
        key_guess = None
        for kname, k in XOR_KEYS.items():
            if img_lo <= (entry_masked ^ k["entry"]) < img_hi:
                key_guess = kname
                break
        if not entry_in_image:
            if key_guess:
                self.warn(f"entry point unmasked with '{self.key}' key (0x{entry:08X}) lies outside the "
                          f"image; the '{key_guess}' key gives 0x{entry_masked ^ XOR_KEYS[key_guess]['entry']:08X} "
                          f"which is inside — rerun with --key {key_guess}")
            else:
                self.warn(f"entry point 0x{entry:08X} lies outside [0x{img_lo:08X}, 0x{img_hi:08X}) "
                          f"for every known key")
        if not (img_lo <= thunk < img_hi):
            self.warn(f"kernel thunk 0x{thunk:08X} lies outside the image for key '{self.key}'")

        # 3. Sections -------------------------------------------------------------
        n_sec = hdr["NumberOfSections"]
        sec_va = hdr["SectionHeadersAddress"]
        if n_sec > 1024:
            raise XbeError(f"NumberOfSections = {n_sec} is implausible (>1024)")
        sec_off = self.va_to_offset(sec_va, n_sec * SECTION_HEADER_SIZE) if n_sec else 0
        if n_sec and sec_off is None:
            # Section table almost always lives in the header; if VA translation
            # failed, fall back to a raw header-relative offset with a warning.
            if self.base_address <= sec_va < self.base_address + len(self.data):
                sec_off = sec_va - self.base_address
                self.warn(f"SectionHeadersAddress 0x{sec_va:08X} is outside SizeOfHeaders; "
                          f"using base-relative offset 0x{sec_off:X}")
            else:
                raise XbeError(f"SectionHeadersAddress 0x{sec_va:08X} cannot be mapped to file data")

        # First pass: raw section records (names need the table to exist for VA translation
        # of names that live inside a section rather than the header).
        raw_sections = []
        for i in range(n_sec):
            off = sec_off + i * SECTION_HEADER_SIZE
            self._need(off, SECTION_HEADER_SIZE, f"section header #{i}")
            raw_sections.append(struct.unpack_from(SECTION_HEADER_FMT, self.data, off))

        for i, rec in enumerate(raw_sections):
            (flags, vaddr, vsize, raddr, rsize, name_va,
             _name_ref, _head_ref, _tail_ref, digest) = rec
            in_bounds = raddr + rsize <= len(self.data)
            if not in_bounds:
                self.warn(f"section #{i}: raw data 0x{raddr:X}+0x{rsize:X} extends past end of file")
            if vsize < rsize:
                self.warn(f"section #{i}: VirtualSize (0x{vsize:X}) < RawSize (0x{rsize:X})")
            if not (img_lo <= vaddr and vaddr + vsize <= img_hi):
                self.warn(f"section #{i}: VA range 0x{vaddr:08X}+0x{vsize:X} exceeds SizeOfImage")
            self.sections.append(Section(
                index=i, name="", flags=flags,
                flag_names=self._flag_names(flags, SECTION_FLAGS),
                virtual_address=vaddr, virtual_size=vsize,
                raw_address=raddr, raw_size=rsize,
                name_address=name_va, digest=digest.hex(),
                raw_in_bounds=in_bounds,
            ))
        # Second pass: resolve names now that va_to_offset can see the sections.
        for s in self.sections:
            s.name = self.read_cstring(s.name_address, 64, f"section #{s.index} name")

        # 4. Library versions ------------------------------------------------------
        n_lib = hdr["NumberOfLibraryVersions"]
        lib_va = hdr["LibraryVersionsAddress"]
        libs: List[LibraryVersion] = []
        if n_lib > 256:
            self.warn(f"NumberOfLibraryVersions = {n_lib} is implausible; clamping to 256")
            n_lib = 256
        if n_lib:
            lib_off = self.va_to_offset(lib_va, n_lib * LIBRARY_VERSION_SIZE)
            if lib_off is None:
                self.warn(f"LibraryVersionsAddress 0x{lib_va:08X} cannot be mapped to file data; "
                          f"skipping library table")
            else:
                for i in range(n_lib):
                    off = lib_off + i * LIBRARY_VERSION_SIZE
                    self._need(off, LIBRARY_VERSION_SIZE, f"library version #{i}")
                    libs.append(self._parse_libver(i, off))
        # The kernel and XAPI records are normally *inside* the array; if they point
        # elsewhere (some early XDKs), parse them separately so nothing is missed.
        for label, va in (("kernel", hdr["KernelLibraryVersionAddress"]),
                          ("xapi", hdr["XapiLibraryVersionAddress"])):
            if not va:
                continue
            off = self.va_to_offset(va, LIBRARY_VERSION_SIZE)
            if off is None:
                self.warn(f"{label} library version VA 0x{va:08X} is unmapped")
                continue
            if libs and lib_off is not None and lib_off <= off < lib_off + n_lib * LIBRARY_VERSION_SIZE:
                continue  # already covered by the array
            libs.append(self._parse_libver(len(libs), off))
            self.warn(f"{label} library version record at 0x{va:08X} lies outside the main array; appended")

        # 5. Certificate (bonus: TitleId is needed by later stages) ---------------------
        cert = None
        cert_va = hdr["CertificateAddress"]
        if cert_va:
            cert_off = self.va_to_offset(cert_va, CERT_HEAD_SIZE)
            if cert_off is None:
                self.warn(f"CertificateAddress 0x{cert_va:08X} is unmapped; certificate skipped")
            else:
                (csize, ctime, title_id, title_name_raw, _alt_ids, allowed_media,
                 region, ratings, disk, version) = struct.unpack_from(CERT_HEAD_FMT, self.data, cert_off)
                title_name = title_name_raw.decode("utf-16-le", errors="replace").split("\x00", 1)[0]
                cert = Certificate(
                    address=cert_va, size=csize, time_date=ctime,
                    title_id=title_id, title_id_hex=f"{title_id:08X}",
                    title_name=title_name, allowed_media=allowed_media,
                    game_region=region, game_region_names=self._flag_names(region, GAME_REGION),
                    game_ratings=ratings, disk_number=disk, version=version,
                )

        debug_path = self.read_cstring(hdr["DebugPathnameAddress"], 260, "debug pathname") \
            if hdr["DebugPathnameAddress"] else ""

        return XbeInfo(
            path=self.path,
            file_size=len(self.data),
            magic_ok=True,
            key_used=self.key,
            base_address=self.base_address,
            size_of_headers=self.size_of_headers,
            size_of_image=self.size_of_image,
            size_of_image_header=hdr["SizeOfImageHeader"],
            time_date=hdr["TimeDate"],
            time_date_iso=self._iso(hdr["TimeDate"]),
            entry_point_masked=entry_masked,
            entry_point=entry,
            entry_point_in_image=entry_in_image,
            entry_point_key_guess=key_guess,
            kernel_thunk_masked=thunk_masked,
            kernel_thunk=thunk,
            tls_address=hdr["TlsAddress"],
            init_flags=hdr["InitFlags"],
            init_flag_names=self._flag_names(hdr["InitFlags"], INIT_FLAGS),
            pe_base_address=hdr["PeBaseAddress"],
            pe_size_of_image=hdr["PeSizeOfImage"],
            debug_pathname=debug_path,
            number_of_sections=n_sec,
            section_headers_address=sec_va,
            number_of_library_versions=hdr["NumberOfLibraryVersions"],
            library_versions_address=lib_va,
            kernel_library_version_address=hdr["KernelLibraryVersionAddress"],
            xapi_library_version_address=hdr["XapiLibraryVersionAddress"],
            certificate=cert,
            sections=self.sections,
            library_versions=libs,
            warnings=self.warnings,
        )

    def _parse_libver(self, index: int, off: int) -> LibraryVersion:
        name_raw, major, minor, build, flags = struct.unpack_from(LIBRARY_VERSION_FMT, self.data, off)
        name = name_raw.split(b"\x00", 1)[0].decode("ascii", errors="replace").strip()
        qfe = flags & 0x1FFF
        approved = LIB_APPROVED[(flags >> 13) & 0x3]
        debug = bool(flags & 0x8000)
        return LibraryVersion(
            index=index, name=name, major=major, minor=minor, build=build,
            qfe=qfe, approved=approved, debug_build=debug,
            version_string=f"{major}.{minor}.{build}.{qfe}",
        )


# --------------------------------------------------------------------------
# Reporting
# --------------------------------------------------------------------------

def print_report(info: XbeInfo, out=sys.stdout) -> None:
    p = lambda *a: print(*a, file=out)  # noqa: E731

    p("=" * 78)
    p(f"XBE: {info.path}  ({info.file_size:,} bytes)")
    p("=" * 78)
    p(f"  Magic ................. 'XBEH'  OK")
    p(f"  BaseAddress ........... 0x{info.base_address:08X}")
    p(f"  SizeOfHeaders ......... 0x{info.size_of_headers:08X}  ({info.size_of_headers:,})")
    p(f"  SizeOfImage ........... 0x{info.size_of_image:08X}  ({info.size_of_image:,})")
    p(f"  SizeOfImageHeader ..... 0x{info.size_of_image_header:08X}")
    p(f"  TimeDate .............. 0x{info.time_date:08X}  {info.time_date_iso}")
    p(f"  AddressOfEntryPoint ... 0x{info.entry_point_masked:08X} (masked)  ->  "
      f"0x{info.entry_point:08X}  [{info.key_used} key 0x{XOR_KEYS[info.key_used]['entry']:08X}]"
      + ("" if info.entry_point_in_image else "  ** OUTSIDE IMAGE **"))
    p(f"  KernelImageThunkAddr .. 0x{info.kernel_thunk_masked:08X} (masked)  ->  0x{info.kernel_thunk:08X}")
    p(f"  TlsAddress ............ 0x{info.tls_address:08X}")
    p(f"  InitFlags ............. 0x{info.init_flags:08X}  {' '.join(info.init_flag_names) or '-'}")
    p(f"  PE BaseAddress/Size ... 0x{info.pe_base_address:08X} / 0x{info.pe_size_of_image:08X}")
    if info.debug_pathname:
        p(f"  DebugPathname ......... {info.debug_pathname}")

    c = info.certificate
    if c:
        p("")
        p(f"  Certificate @ 0x{c.address:08X}")
        p(f"    TitleId ............. 0x{c.title_id_hex}")
        p(f"    TitleName ........... {c.title_name!r}")
        p(f"    Region .............. 0x{c.game_region:08X}  {' '.join(c.game_region_names) or '-'}")
        p(f"    AllowedMedia ........ 0x{c.allowed_media:08X}")
        p(f"    DiskNumber/Version .. {c.disk_number} / 0x{c.version:08X}")

    p("")
    p(f"Sections ({info.number_of_sections}) @ 0x{info.section_headers_address:08X}")
    p("-" * 78)
    p(f"  {'#':>2}  {'Name':<10} {'VirtAddr':>10} {'VirtSize':>10} {'RawAddr':>10} {'RawSize':>10}  Flags")
    for s in info.sections:
        flags = ",".join(f[:1] + f[1:].lower() for f in s.flag_names) or "-"
        mark = "" if s.raw_in_bounds else "  !truncated"
        p(f"  {s.index:>2}  {s.name:<10} 0x{s.virtual_address:08X} 0x{s.virtual_size:08X} "
          f"0x{s.raw_address:08X} 0x{s.raw_size:08X}  {flags}{mark}")

    p("")
    p(f"Library Versions ({len(info.library_versions)}) @ 0x{info.library_versions_address:08X}")
    p("-" * 78)
    p(f"  {'#':>2}  {'Library':<10} {'Version':<18} {'QFE':>5}  {'Approval':<18} Build")
    for l in info.library_versions:
        build = "DEBUG" if l.debug_build else "retail"
        p(f"  {l.index:>2}  {l.name:<10} {l.major}.{l.minor}.{l.build:<12} {l.qfe:>5}  {l.approved:<18} {build}")

    if info.warnings:
        p("")
        p(f"Warnings ({len(info.warnings)})")
        p("-" * 78)
        for w in info.warnings:
            p(f"  ! {w}")
    p("")


def to_json(info: XbeInfo) -> str:
    d = asdict(info)
    return json.dumps(d, indent=2)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        prog="xbe_parse.py",
        description="Parse an original Xbox .xbe header (Xita pipeline, Stage 1).",
    )
    ap.add_argument("xbe", help="path to the .xbe file")
    ap.add_argument("--json", action="store_true", help="emit machine-readable JSON instead of a report")
    ap.add_argument("--key", choices=sorted(XOR_KEYS), default="retail",
                    help="kernel XOR key set used to unmask EntryPoint/KernelThunk (default: retail)")
    ap.add_argument("--strict", action="store_true",
                    help="exit non-zero if any warnings were raised")
    args = ap.parse_args(argv)

    try:
        with open(args.xbe, "rb") as f:
            data = f.read()
    except OSError as e:
        print(f"error: cannot read {args.xbe}: {e}", file=sys.stderr)
        return 2

    try:
        info = XbeParser(data, args.xbe, key=args.key).parse()
    except XbeError as e:
        print(f"error: {args.xbe}: {e}", file=sys.stderr)
        return 1
    except struct.error as e:
        print(f"error: {args.xbe}: truncated structure ({e})", file=sys.stderr)
        return 1

    if args.json:
        print(to_json(info))
    else:
        print_report(info)

    if args.strict and info.warnings:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
