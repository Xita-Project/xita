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
import zipfile

BUTTONS = dict(select=1, start=8, up=16, right=32, down=64, left=128,
               l=256, r=512, triangle=4096, circle=8192, cross=16384, square=32768)
BENCHMARK_KINDS=("object-basis","model-palette","vertex-worker","vertex-references","native-bounds","vertex-copy","draw-scan","flare","resolution","early-visibility","point-math","texture-state","matrix-neon","object-scan","hle-dispatch","flare-query-overlap","guest-affinity","snapshot-worker","guest-phases","prep-bundle","object-jobs","vertex-prepare","depth-prepare","object-math","object-lock","object-wait","object-point","model-hierarchy","object-quat")
RESULT = re.compile(r"\[([a-z-]+-compare|resolution-test)\] result (?:off-before|544-before) ([\d.]+) (?:on|360) ([\d.]+) (?:off-after|544-after) ([\d.]+) fps comparable-view ([01])")
RESTORED = re.compile(rb"\[(?:[a-z-]+-compare|resolution-test)\] restored [^\n]*\n")


class Client:
    def __init__(self, path):
        self.config = json.loads(Path(path).read_text())
        self.key = self.config["key"]
        if not re.fullmatch(r"[0-9a-fA-F]{32}", self.key):
            raise ValueError("Invalid pairing key")

    def request(self, path, method="GET", body=None, timeout=25):
        conn = http.client.HTTPConnection(self.config["host"], self.config.get("port", 8080), timeout=timeout)
        try:
            conn.request(method, path, body=body, headers={"Authorization": "Bearer " + self.key})
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

    def log(self, path, *, launcher=False, previous=0):
        # Pin the first response's file length, so continuous logging cannot
        # make a pull run indefinitely. App restart/rotation requires a new pull.
        if type(previous) is not int or previous not in range(4) or (launcher and previous):
            raise ValueError("Previous game log must be 0–3; launcher history is unavailable")
        offset = 0
        limit = None
        with Path(path).open("xb") as out:
            while limit is None or offset < limit:
                endpoint="launcher-log" if launcher else f"log/{previous}" if previous else "log"
                headers, data = self.request(f"/{endpoint}?offset={offset}")
                if previous and headers.get("x-log-run") != str(previous):
                    raise RuntimeError("Server did not confirm the requested previous log")
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

    def wait_benchmark_log(self, offset, timeout=30):
        # Vita3K buffers its file writer independently of its status endpoint.
        # Wait for the restoration record to become readable, using only newly
        # appended bytes. No bulk reads happen during the measured phases.
        deadline=time.monotonic()+timeout
        tail=b""
        while time.monotonic()<deadline:
            headers,data=self.request(f"/log?offset={offset}")
            size=int(headers["x-log-size"])
            if size<offset or len(data)>size-offset:
                raise RuntimeError("Log changed during result collection")
            tail+=data;offset+=len(data)
            if len(tail)>2*1024*1024:
                raise RuntimeError("Benchmark result tail exceeded collection limit")
            if RESTORED.search(tail):return
            if offset==size:time.sleep(.5)
        raise RuntimeError("Benchmark restored status, but its final log record did not arrive")


def wait_for_update(client, sha, previous_slot, timeout=180):
    deadline=time.monotonic()+timeout
    last_stage=None
    stages={1:"request accepted",2:"recording drained",3:"render worker stopped",4:"GPU drain",5:"display drain",6:"network shutdown",7:"launcher handoff"}
    while time.monotonic()<deadline:
        time.sleep(1)
        try:
            boot=json.loads(client.request("/update")[1])
        except (OSError,RuntimeError,http.client.HTTPException):
            continue
        stage=boot.get("handoff",0)
        if stage and stage!=last_stage:
            last_stage=stage
            print("Update handoff: "+stages.get(stage,f"stage {stage}"),flush=True)
        if not boot["requested"] and boot.get("boot_slot",-1)>=0:
            if boot.get("boot_sha256") != sha or boot["boot_slot"] == previous_slot or boot["state"] != 0:
                raise RuntimeError("Update did not install: launcher returned to the previous slot or left the candidate staged. The working build is preserved.")
            return boot["boot_slot"]
    detail=" Last reported stage: "+stages.get(last_stage,f"stage {last_stage}")+"." if last_stage else ""
    raise RuntimeError("No confirmed boot within the timeout; installation was not verified."+detail+" Reconnect to inspect update status.")


def upload_update(client, package, apply=False, wait=False):
    from package_vpk import update_contract
    with zipfile.ZipFile(package) as z:
        names=z.namelist()
        if len(set(names)) != len(names) or any(i.file_size > 64*1024*1024 for i in z.infolist()) or sum(i.file_size for i in z.infolist()) > 256*1024*1024:
            raise ValueError("Invalid or oversized update package")
        files={name:z.read(name) for name in names}
    required={"eboot.bin", "game-a.self", "update-contract.txt", "boot-game.txt"}
    if not required.issubset(files):
        raise ValueError("This VPK does not include the integrated updater")
    abi=update_contract(files)
    if files["update-contract.txt"] != (abi+"\n").encode():
        raise ValueError("Package asset contract failed")
    data=files["game-a.self"]
    if not 4096 <= len(data) <= 64*1024*1024 or data[:4] != b"SCE\0":
        raise ValueError("Invalid Vita executable")
    current=json.loads(client.request("/update")[1])
    if current["contract"] != abi:
        raise ValueError("Launcher or packaged assets differ; install this VPK once through VitaShell")
    if apply and current.get("boot_slot",-1) not in (0,1):
        raise ValueError("Wait for a confirmed dashboard boot before applying an update")
    sha=hashlib.sha256(data).hexdigest()
    client.lease(1800)
    client.request(f"/update/begin?size={len(data)}&sha256={sha}&contract={abi}", "POST")
    for offset in range(0,len(data),65536):
        client.request(f"/update/chunk?offset={offset}","POST",data[offset:offset+65536])
        if offset % (1024*1024) == 0:
            print(f"Uploaded {min(offset+65536,len(data))}/{len(data)} bytes",flush=True)
    client.request("/update/finish","POST",timeout=120)
    state=json.loads(client.request("/update")[1])
    if state["state"] != 2 or state["received"] != len(data):
        raise RuntimeError("Device did not confirm verified update")
    result={"bytes":len(data),"sha256":sha,"verified":True,"restart_requested":False}
    if apply:
        client.request("/update/apply","POST")
        result["restart_requested"]=True
        if wait:
            result["slot"]=wait_for_update(client,sha,current["boot_slot"])
            result["boot_confirmed"]=True
    print(json.dumps(result),flush=True)
    return result


def benchmark(client, out, runs, timeout, kind=None):
    if not 1 <= runs <= 10 or not 30 <= timeout <= 600:
        raise ValueError("Use 1–10 trials and a 30–600 second timeout per trial")
    if kind is not None and kind not in BENCHMARK_KINDS:
        raise ValueError("Unknown benchmark kind")
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
        log_offset=(out/"before.log").stat().st_size
        prior = len(RESULT.findall((out / "before.log").read_text(errors="replace")))
        for i in range(runs):
            if client.status()["benchmark"]:
                raise RuntimeError("Unexpected active benchmark before trial")
            if kind is None:
                client.hold(BUTTONS["l"] | BUTTONS["r"] | BUTTONS["square"], duration=.6)
            else:
                client.request("/benchmark?kind="+kind,"POST")
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
            client.wait_benchmark_log(log_offset)
            logfile = out / f"trial-{i+1}.log"
            trial_offset=log_offset
            client.log(logfile)
            log_offset=logfile.stat().st_size
            text = logfile.read_text(errors="replace")
            if kind=="guest-affinity" and "[guest-affinity] failure" in logfile.read_bytes()[trial_offset:].decode(errors="replace"):
                raise RuntimeError("Affinity change or restoration failed; no valid comparison claimed")
            matches = RESULT.findall(text)
            if len(matches) != prior + 1:
                raise RuntimeError("Expected one fresh benchmark result; cancelled/incomplete run or restarted application")
            tag, before, on, after, comparable = matches[-1]
            expected="resolution-test" if kind=="resolution" else kind+"-compare" if kind else None
            if expected and tag!=expected:
                raise RuntimeError("Completed a different benchmark than requested")
            if comparable != "1":
                raise RuntimeError("Camera consistency check failed; trial is not comparable")
            trial = dict(candidate=tag, off_before_fps=float(before), on_fps=float(on), off_after_fps=float(after),
                         log=str(logfile), comparable=True)
            if kind=="guest-phases":
                trial["diagnostic"]=True
                trial["note"]="Timing is enabled only in the middle arm. This measures profiling overhead, not an optimization gain; already-open parent scopes are absent."
            if kind=="resolution":
                trial["fps_544_before"]=trial.pop("off_before_fps")
                trial["fps_360"]=trial.pop("on_fps")
                trial["fps_544_after"]=trial.pop("off_after_fps")
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
    upload = commands.add_parser("update", help="stage a compatible VPK executable and verify on device")
    upload.add_argument("package", type=Path)
    upload.add_argument("--apply", action="store_true", help="restart into the verified candidate")
    commands.add_parser("update-status")
    commands.add_parser("rollback", help="restart into the previous confirmed executable")
    shot = commands.add_parser("screen"); shot.add_argument("output", type=Path)
    log = commands.add_parser("log"); log.add_argument("output", type=Path)
    log.add_argument("--previous", type=int, choices=range(4), default=0,
                     help="0=current run; 1–3 select saved runs after a restart (requires updated runtime)")
    launcher_log = commands.add_parser("launcher-log"); launcher_log.add_argument("output", type=Path)
    lease = commands.add_parser("lease"); lease.add_argument("seconds", type=int)
    pad = commands.add_parser("pad")
    pad.add_argument("buttons", nargs="*", choices=list(BUTTONS))
    pad.add_argument("--duration", type=float, default=.45)
    for axis in ("lx", "ly", "rx", "ry"):
        pad.add_argument("--" + axis, type=int, default=128)
    bench = commands.add_parser("benchmark")
    bench.add_argument("output", type=Path); bench.add_argument("--runs", type=int, default=3)
    bench.add_argument("--timeout", type=int, default=180)
    bench.add_argument("--kind", choices=BENCHMARK_KINDS, help="select a test for this run without changing saved settings")
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
    if args.command == "update":
        upload_update(client,args.package,args.apply,wait=args.apply)
    elif args.command == "update-status":
        print(client.request("/update")[1].decode())
    elif args.command == "rollback":
        client.request("/update/rollback","POST")
    elif args.command == "status":
        print(json.dumps(client.status(), indent=2))
    elif args.command == "screen":
        print(json.dumps(client.screen(args.output)))
    elif args.command == "log":
        print(f"Saved {client.log(args.output,previous=args.previous)} bytes")
    elif args.command == "launcher-log":
        print(f"Saved {client.log(args.output,launcher=True)} bytes")
    elif args.command == "release":
        client.pad()
    elif args.command == "lease":
        client.lease(args.seconds)
    elif args.command == "pad":
        client.hold(sum(BUTTONS[b] for b in set(args.buttons)), args.duration,
                    **{a: getattr(args, a) for a in ("lx", "ly", "rx", "ry")})
    elif args.command == "benchmark":
        benchmark(client, args.output, args.runs, args.timeout,args.kind)


if __name__ == "__main__":
    main()
