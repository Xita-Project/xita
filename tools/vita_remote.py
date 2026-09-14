#!/usr/bin/env python3
"""Authenticated LAN controls for Xita's opt-in hardware test service."""
import argparse
import hashlib
import http.client
import io
import json
import os
from pathlib import Path
import re
import secrets
import time

BUTTONS = dict(select=1, start=8, up=16, right=32, down=64, left=128,
               l=256, r=512, triangle=4096, circle=8192, cross=16384, square=32768)
RESULT = re.compile(r"\[([a-z-]+-compare)\] result off-before ([\d.]+) on ([\d.]+) off-after ([\d.]+) fps comparable-view ([01])")


class Client:
    def __init__(self, path):
        self.config = json.loads(Path(path).read_text())
        self.key = self.config["key"]
        if not re.fullmatch(r"[0-9a-fA-F]{32}", self.key):
            raise ValueError("Invalid pairing key")

    def request(self, path, method="GET"):
        conn = http.client.HTTPConnection(self.config["host"], self.config.get("port", 8080), timeout=25)
        try:
            conn.request(method, path, headers={"Authorization": "Bearer " + self.key})
            response = conn.getresponse()
            data = response.read()
            if response.getheader("Content-Length") != str(len(data)):
                raise RuntimeError("Invalid or incomplete HTTP response length")
            if response.status not in (200, 204):
                raise RuntimeError(f"Vita HTTP {response.status}: {data.decode(errors='replace').strip()}")
            return {k.lower(): v for k, v in response.getheaders()}, data
        finally:
            conn.close()

    def status(self):
        return json.loads(self.request("/status")[1])

    def lease(self, seconds):
        if not 0 <= seconds <= 3600:
            raise ValueError("Keep-awake lease must be 0–3600 seconds")
        self.request(f"/lease?seconds={seconds}", "POST")

    def pad(self, buttons=0, lx=128, ly=128, rx=128, ry=128, ms=0):
        if any(not 0 <= axis <= 255 for axis in (lx, ly, rx, ry)) or not 0 <= ms <= 2000:
            raise ValueError("Stick values must be 0–255 and input lease 0–2000 ms")
        self.request(f"/pad?buttons={buttons}&lx={lx}&ly={ly}&rx={rx}&ry={ry}&ms={ms}", "POST")

    def hold(self, buttons=0, duration=.45, **axes):
        if not 0 < duration <= 60:
            raise ValueError("Hold duration must be greater than zero and at most 60 seconds")
        end = time.monotonic() + duration
        try:
            while time.monotonic() < end:
                remaining = end - time.monotonic()
                self.pad(buttons, **axes, ms=min(2000, max(1, int(remaining * 1000) + 100)))
                time.sleep(min(remaining, 1))
        finally:
            self.pad()  # Device timeout also releases input if the connection is lost.

    def screen(self, path):
        headers, data = self.request("/screen")
        path = Path(path)
        if path.suffix.lower() == ".png":
            from PIL import Image
            Image.open(io.BytesIO(data)).save(path)
        else:
            path.write_bytes(data)
        return {"path": str(path), "frame": int(headers["x-xita-frame"]),
                "ppm_sha256": hashlib.sha256(data).hexdigest()}

    def log(self, path):
        # Pin the first response's file length, so continuous logging cannot
        # make a pull run indefinitely. App restart/rotation requires a new pull.
        offset = 0
        limit = None
        with Path(path).open("xb") as out:
            while limit is None or offset < limit:
                headers, data = self.request(f"/log?offset={offset}")
                size = int(headers["x-log-size"])
                if limit is None:
                    limit = size
                if size < limit:
                    raise RuntimeError("Log shrank during collection; preserve this partial capture and retry separately")
                data = data[:limit - offset]
                if not data and offset < limit:
                    raise RuntimeError("Log ended before the declared size")
                out.write(data); offset += len(data)
        return offset


def benchmark(client, out, runs, timeout):
    if not 1 <= runs <= 10 or not 30 <= timeout <= 600:
        raise ValueError("Use 1–10 trials and a 30–600 second timeout per trial")
    out = Path(out); out.mkdir(parents=True, exist_ok=False)
    receipt = {"trials": [], "complete": False,
               "note": "Same-session off/on/off; live simulation continues. Network status polling remains enabled in every arm."}
    try:
        receipt["initial"] = client.status()
        if receipt["initial"]["benchmark"]:
            raise RuntimeError("A benchmark is already active")
        client.lease(min(3600, runs * timeout + 60))
        receipt["before"] = client.screen(out / "before.ppm")
        client.log(out / "before.log")
        prior = len(RESULT.findall((out / "before.log").read_text(errors="replace")))
        for i in range(runs):
            if client.status()["benchmark"]:
                raise RuntimeError("Unexpected active benchmark before trial")
            client.hold(BUTTONS["l"] | BUTTONS["r"] | BUTTONS["square"], duration=.6)
            deadline = time.monotonic() + timeout
            seen_active = False
            while time.monotonic() < deadline:
                status = client.status()
                seen_active |= bool(status["benchmark"])
                if seen_active and not status["benchmark"]:
                    break
                if not seen_active and deadline - time.monotonic() < timeout - 5:
                    raise RuntimeError("Benchmark did not start; enter a loaded first-person view before running this command")
                time.sleep(1)
            else:
                raise RuntimeError("Benchmark deadline exceeded; no completed result claimed")
            time.sleep(.5)  # allow restoration's final log line to reach the sink
            logfile = out / f"trial-{i+1}.log"
            client.log(logfile)
            matches = RESULT.findall(logfile.read_text(errors="replace"))
            if len(matches) != prior + 1:
                raise RuntimeError("Expected one fresh benchmark result; cancelled/incomplete run or restarted application")
            tag, before, on, after, comparable = matches[-1]
            if comparable != "1":
                raise RuntimeError("Camera consistency check failed; trial is not comparable")
            trial = dict(candidate=tag, off_before_fps=float(before), on_fps=float(on), off_after_fps=float(after),
                         log=str(logfile), comparable=True)
            receipt["trials"].append(trial); prior = len(matches)
            print(json.dumps(trial), flush=True)
            (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        receipt["after"] = client.screen(out / "after.ppm")
        receipt["complete"] = True
    except BaseException as exc:
        receipt["error"] = str(exc)
        raise
    finally:
        (out / "result.json").write_text(json.dumps(receipt, indent=2) + "\n")
        try:
            client.pad()
        except (OSError, RuntimeError):
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, help="Private remote-client.json pairing file")
    commands = parser.add_subparsers(dest="command", required=True)
    pair = commands.add_parser("pair", help="Generate private credentials; no device writes")
    pair.add_argument("directory", type=Path)
    pair.add_argument("--host", required=True)
    pair.add_argument("--port", type=int, default=8080)
    commands.add_parser("status")
    commands.add_parser("release")
    shot = commands.add_parser("screen"); shot.add_argument("output", type=Path)
    log = commands.add_parser("log"); log.add_argument("output", type=Path)
    lease = commands.add_parser("lease"); lease.add_argument("seconds", type=int)
    pad = commands.add_parser("pad")
    pad.add_argument("buttons", nargs="*", choices=list(BUTTONS))
    pad.add_argument("--duration", type=float, default=.45)
    for axis in ("lx", "ly", "rx", "ry"):
        pad.add_argument("--" + axis, type=int, default=128)
    bench = commands.add_parser("benchmark")
    bench.add_argument("output", type=Path); bench.add_argument("--runs", type=int, default=3)
    bench.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    if args.command == "pair":
        if not 1024 <= args.port <= 65535:
            parser.error("Port must be 1024–65535")
        args.directory.mkdir(parents=True, exist_ok=False, mode=0o700)
        key = secrets.token_hex(16)
        for name, data in [("remote.key", key + "\n"), ("remote-client.json", json.dumps(dict(host=args.host, port=args.port, key=key), indent=2) + "\n")]:
            fd = os.open(args.directory / name, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            with os.fdopen(fd, "w") as f:
                f.write(data)
        print(f"Created private pairing files in {args.directory}. Copy remote.key to ux0:data/xita/ and enable XV_REMOTE_TEST=1; restart Xita.")
        return
    if not args.config:
        parser.error("--config is required")
    client = Client(args.config)
    if args.command == "status":
        print(json.dumps(client.status(), indent=2))
    elif args.command == "screen":
        print(json.dumps(client.screen(args.output)))
    elif args.command == "log":
        print(f"Saved {client.log(args.output)} bytes")
    elif args.command == "release":
        client.pad()
    elif args.command == "lease":
        client.lease(args.seconds)
    elif args.command == "pad":
        client.hold(sum(BUTTONS[b] for b in set(args.buttons)), args.duration,
                    **{a: getattr(args, a) for a in ("lx", "ly", "rx", "ry")})
    elif args.command == "benchmark":
        benchmark(client, args.output, args.runs, args.timeout)


if __name__ == "__main__":
    main()
