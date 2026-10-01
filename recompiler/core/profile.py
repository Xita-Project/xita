"""Strict, versioned JSON game profiles. No additional Python dependency."""
from __future__ import annotations
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
SLUG = re.compile(r"[a-z][a-z0-9_]*\Z")
DIGEST = re.compile(r"[0-9a-f]{64}\Z")


def fields(value, allowed, required, where):
    if not isinstance(value, dict):
        raise ValueError(f"{where} must be an object")
    if set(value) - set(allowed):
        raise ValueError(f"{where}: unknown fields {sorted(set(value) - set(allowed))}")
    if set(required) - set(value):
        raise ValueError(f"{where}: missing fields {sorted(set(required) - set(value))}")


def uint32(value, where):
    if type(value) is int:
        number = value
    elif isinstance(value, str) and re.fullmatch(r"(?:0x[0-9a-fA-F]+|[0-9]+)", value):
        number = int(value, 16 if value.startswith("0x") else 10)
    else:
        raise ValueError(f"{where} must be an unsigned integer or hex string")
    if not 0 <= number <= 0xFFFFFFFF:
        raise ValueError(f"{where} is outside uint32 range")
    return number


def identifier(value, where):
    if not isinstance(value, str) or not IDENTIFIER.fullmatch(value):
        raise ValueError(f"{where} must be a C identifier")
    return value


def digest(value, where):
    if not isinstance(value, str) or not DIGEST.fullmatch(value):
        raise ValueError(f"{where} must be a lowercase SHA-256 digest")
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


@dataclass(frozen=True)
class GameProfile:
    path: Path
    id: str
    name: str
    binary: dict
    overrides: dict
    roots: tuple
    lift: tuple
    variables: dict
    adapter: str | None
    symbols_sha256: str | None

    def validate_image(self, image):
        """Reject another revision or a stale/edited parser manifest before writing."""
        from recompiler.xbe_parse import XbeParser, to_json
        if hashlib.sha256(image.data).hexdigest() != self.binary["sha256"]:
            raise ValueError(f"{self.id}: XBE SHA-256 mismatch; this profile is revision-specific")
        actual = json.loads(to_json(XbeParser(image.data, "<profile input>").parse()))
        cert = actual.get("certificate") or {}
        if cert.get("title_id") != self.binary["title_id"]:
            raise ValueError(f"{self.id}: XBE title ID mismatch")
        for key in ("base_address", "entry_point", "size_of_image"):
            if actual[key] != self.binary[key]:
                raise ValueError(f"{self.id}: binary.{key} does not match the XBE")
        # The lifter trusts all these fields. A correct XBE with someone else's
        # manifest must not map address-based hooks onto different file bytes.
        for key in ("base_address", "entry_point", "size_of_image", "kernel_thunk", "tls_address", "sections"):
            if image.m.get(key) != actual[key]:
                raise ValueError(f"{self.id}: stale or mismatched manifest field {key}")
        for address in (*self.roots, *self.overrides):
            if not image.is_code(address):
                raise ValueError(f"{self.id}: profile address 0x{address:X} is not executable")
        for name, address in self.variables.items():
            if image.section_of(address) is None:
                raise ValueError(f"{self.id}: variable {name} is outside the image")

    def validate_symbols(self, data):
        if self.symbols_sha256 is not None:
            if data is None or hashlib.sha256(data).hexdigest() != self.symbols_sha256:
                raise ValueError(f"{self.id}: matching --symbols file required (SHA-256 mismatch or missing)")


def load_profile(name):
    path = Path(name)
    if SLUG.fullmatch(str(name)):
        path = ROOT / "games" / str(name) / "profile.json"
    doc = json.loads(path.read_text(), object_pairs_hook=unique_object)
    fields(doc, {"schema_version", "id", "name", "binary", "function_overrides", "roots", "lift", "variables", "adapter", "symbols_sha256"},
           {"schema_version", "id", "name", "binary"}, "profile")
    if type(doc["schema_version"]) is not int or doc["schema_version"] != 1:
        raise ValueError("Unsupported profile schema_version (expected 1)")
    if not isinstance(doc["id"], str) or not SLUG.fullmatch(doc["id"]):
        raise ValueError("profile.id must be a lowercase identifier")
    if not isinstance(doc["name"], str) or not doc["name"].strip():
        raise ValueError("profile.name must be a nonempty string")
    binary = dict(doc["binary"]) if isinstance(doc["binary"], dict) else doc["binary"]
    required = {"sha256", "title_id", "base_address", "entry_point", "size_of_image"}
    fields(binary, required, required, "binary")
    binary["sha256"] = digest(binary["sha256"], "binary.sha256")
    for key in required - {"sha256"}:
        binary[key] = uint32(binary[key], f"binary.{key}")
    base, size = binary["base_address"], binary["size_of_image"]
    if not size or base + size > 0x100000000 or not base <= binary["entry_point"] < base + size:
        raise ValueError("binary entry point/image range is invalid")
    adapter = doc.get("adapter")
    from games import ADAPTERS
    if adapter is not None and (not isinstance(adapter, str) or adapter not in ADAPTERS):
        raise ValueError("Unknown game adapter; register reviewed code in games/__init__.py")
    overrides, signatures = {}, {}
    raw = doc.get("function_overrides", {})
    fields(raw, raw.keys() if isinstance(raw, dict) else (), (), "function_overrides")
    for key, item in raw.items():
        address = uint32(key, "override address")
        if address in overrides:
            raise ValueError(f"Duplicate override address: 0x{address:X}")
        fields(item, {"name", "stack_args"}, {"name", "stack_args"}, f"override {key}")
        name = identifier(item["name"], f"override {key} name")
        argc = uint32(item["stack_args"], f"override {key} stack_args")
        if argc > 64:
            raise ValueError("stack_args must be in 0..64")
        if name in signatures and signatures[name] != argc:
            raise ValueError(f"Conflicting stack argument counts for {name}")
        signatures[name] = argc
        overrides[address] = {"lib": "CUSTOM", "kind": "FUN", "convention": "stdcall", "name": name,
                              "args": [f"psh a{i}" for i in range(argc)], "address": address}
    roots, lift = doc.get("roots", []), doc.get("lift", [])
    if not isinstance(roots, list) or not isinstance(lift, list):
        raise ValueError("roots and lift must be arrays")
    roots = tuple(uint32(x, "root") for x in roots)
    lift = tuple(identifier(x, "lift symbol") for x in lift)
    if len(set(roots)) != len(roots) or len(set(lift)) != len(lift):
        raise ValueError("Duplicate roots or lift symbols")
    variables = doc.get("variables", {})
    fields(variables, variables.keys() if isinstance(variables, dict) else (), (), "variables")
    variables = {identifier(k, "variable name"): uint32(v, "variable address") for k, v in variables.items()}
    symbols_hash = doc.get("symbols_sha256")
    if symbols_hash is not None:
        symbols_hash = digest(symbols_hash, "symbols_sha256")
    return GameProfile(path.resolve(), doc["id"], doc["name"], binary, overrides,
                       roots, lift, variables, adapter, symbols_hash)


def list_profiles():
    return [load_profile(path) for path in sorted((ROOT / "games").glob("*/profile.json"))]
