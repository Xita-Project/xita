#!/usr/bin/env python3
"""Exercise the production HTTP/input/capture code through real loopback sockets."""
import http.client
import hashlib
import zipfile
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
from vita_remote import Client, benchmark, upload_update, wait_for_update
from package_vpk import update_contract, update_record
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def benchmark_cases(tmp):
    class Fake:
        def __init__(self, mode, kind="object-basis"):
            self.mode = mode; self.kind = kind
            self.active = False; self.records = ""; self.released = False
        def status(self):
            if self.mode == "disconnect":
                raise OSError("connection lost")
            active = self.active; self.active = False
            return {"benchmark": int(active), "frames": 100}
        def lease(self, seconds):
            assert 0 < seconds <= 3600
        def screen(self, path):
            assert not self.active; Path(path).write_bytes(b"test capture")
            return {"frame": 100}
        def log(self, path):
            assert not self.active; Path(path).write_text(self.records)
        def wait_benchmark_log(self, offset):
            assert not self.active
        def hold(self, buttons, duration):
            self.active = True
            if self.mode != "missing-result":
                self.records += "[" + self.kind + "-compare] result off-before 10.000 on 12.000 off-after 10.000 fps comparable-view " + ("0" if self.mode == "camera" else "1") + "\n"
        def pad(self):
            self.released = True
        def request(self,path,method):
            assert path=='/benchmark?kind='+self.kind and method=='POST'
            self.hold(0,0)
    for mode in ("success", "camera", "missing-result", "disconnect"):
        client = Fake(mode); out = tmp / mode
        with patch("vita_remote.time.sleep", lambda _: None):
            try:
                benchmark(client, out, 3, 30)
            except (RuntimeError, OSError):
                assert mode != "success"
            else:
                assert mode == "success"
        result = json.loads((out / "result.json").read_text())
        assert result["complete"] == (mode == "success") and client.released
        assert len(result["trials"]) == (3 if mode == "success" else 0)
        if mode != "success":
            assert result["error"]
    with patch("vita_remote.time.sleep",lambda _:None):
        for kind in ('object-basis', 'matrix-neon'):
            benchmark(Fake('success', kind),tmp/('selected-'+kind),1,30,kind)


def main():
    # Completion status may precede visible file bytes. Incremental polling
    # must accept a split restoration line without re-reading the whole log.
    tail_client=object.__new__(Client)
    chunks=[b'',b'[model-palette-compare] resto',b'red 360p (requested 360p)\n']
    cursor=[100]
    def delayed_log(path):
        assert path==f'/log?offset={cursor[0]}'
        data=chunks.pop(0);cursor[0]+=len(data)
        return {'x-log-size':str(cursor[0])},data
    tail_client.request=delayed_log
    with patch('vita_remote.time.sleep',lambda _:None):tail_client.wait_benchmark_log(100)
    assert not chunks
    # An identical runtime hash must not hide a failed inactive-slot install.
    class Boot:
        def __init__(self,slot,state,sha='a'*64): self.value=dict(requested=0,boot_slot=slot,state=state,boot_sha256=sha)
        def request(self,path): return {},json.dumps(self.value).encode()
    with patch('vita_remote.time.sleep',lambda _:None):
        assert wait_for_update(Boot(1,0),'a'*64,0)==1
        for boot in (Boot(0,2),Boot(0,0),Boot(1,2),Boot(1,0,'b'*64)):
            try: wait_for_update(boot,'a'*64,0)
            except RuntimeError: pass
            else: raise AssertionError('Failed installation acknowledged as installed')
    stalled=Boot(0,4);stalled.value.update(requested=1,handoff=4)
    with patch('vita_remote.time.sleep',lambda _:None), patch('vita_remote.time.monotonic',side_effect=[0,0,2]):
        try: wait_for_update(stalled,'a'*64,0,timeout=1)
        except RuntimeError as error:
            assert 'not verified' in str(error) and 'Last reported stage: GPU drain' in str(error)
        else: raise AssertionError('Stalled handoff acknowledged as installed')
    with tempfile.TemporaryDirectory(prefix="xita-remote-test-") as tmp:
        tmp = Path(tmp)
        benchmark_cases(tmp)
        exe = tmp / "server"
        flags = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if os.getenv("SANITIZE") else []
        subprocess.run(["cc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror", *flags,
                        str(ROOT / "tools/tests/remote_server.c"), str(ROOT / "runtime/xv_update.c"), str(ROOT / "runtime/xv_sha256.c"), "-pthread", "-o", str(exe)], check=True)
        data = tmp / "ux0:data/xita"
        data.mkdir(parents=True)
        key = "0123456789abcdef" * 2
        (data / "remote.key").write_text(key + "\n")
        log = b"frame evidence\n" * 10000
        (data / "xita.log").write_bytes(log)
        (data / "update").mkdir()
        launcher_log=b"update failed: open inactive app slot for writing (errno 13)\n"
        (data / "update/launcher.log").write_bytes(launcher_log)
        app=tmp/'ux0:app/XITA00001';app.mkdir(parents=True)
        (tmp/'app0:').symlink_to(app,target_is_directory=True)
        old=b'SCE\0'+bytes(4092)
        new=b'SCE\0'+bytes(range(256))*700
        files={'eboot.bin':b'SCE\0stable launcher', 'game-a.self':new, 'sce_sys/param.sfo':b'\0PSFtest'}
        abi=update_contract(files)
        files['update-contract.txt']=(abi+'\n').encode()
        files['boot-game.txt']=update_record(len(new),hashlib.sha256(new).hexdigest(),abi)
        (app/'update-contract.txt').write_text(abi+'\n')
        (app/'boot-game.txt').write_bytes(update_record(len(old),hashlib.sha256(old).hexdigest(),abi))
        (app/'game-a.self').write_bytes(old)
        candidate=tmp/'candidate.vpk'
        with zipfile.ZipFile(candidate,'w') as z:
            for name,value in files.items():z.writestr(name,value)
        env = dict(os.environ, XV_REMOTE_TEST="0", XV_NET_ADHOC="0")
        for enabled, adhoc, token in [("0", "0", key), ("1", "1", key), ("1", "0", "bad")]:
            (data / "remote.key").write_text(token)
            assert subprocess.check_output([exe], cwd=tmp, env=dict(env, XV_REMOTE_TEST=enabled, XV_NET_ADHOC=adhoc)) == b"DISABLED\n"
        (data / "remote.key").write_text(key)
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        proc = subprocess.Popen([exe], cwd=tmp, env=dict(env, XV_REMOTE_TEST="1", XV_REMOTE_PORT=str(port)),
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE)

        def command(c):
            proc.stdin.write(c.encode()); proc.stdin.flush()
            return proc.stdout.readline().decode().strip()

        def request(path, method="GET", token=key, data=None):
            conn = http.client.HTTPConnection("127.0.0.1", port, timeout=20)
            conn.request(method, path, body=data, headers={"Authorization": "Bearer " + token})
            r = conn.getresponse(); result = r.status, dict(r.getheaders()), r.read(); conn.close()
            return result

        def raw(data):
            with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
                s.sendall(data)
                return s.recv(4096).split(b" ")[1]

        try:
            assert proc.stdout.readline() == b"READY\n"
            code, _, body = request("/status"); assert code == 200 and json.loads(body)["protocol"] == 1
            assert request("/status", token="f" * 32)[0] == 403
            assert raw(b"GET /status HTTP/1.1\r\nHost: localhost\r\n\r\n") == b"403"
            auth = f"Authorization: Bearer {key}\r\n".encode()
            for extra in [auth, b"Content-Length: 1\r\n", b"Transfer-Encoding: chunked\r\n", b"Content-Length: 0\r\nContent-Length: 0\r\n"]:
                assert raw(b"GET /status HTTP/1.1\r\n" + auth + extra + b"\r\n") == b"403"
            assert raw(b"GET /status HTTP/1.1\r\n" + auth + b"\r\nx") == b"400"
            assert request("/../../save")[0] == 404
            assert request("/pad?buttons=8&lx=128&ly=128&rx=160&ry=128&ms=1000", "POST")[0] == 204
            assert command("p") == "PAD 8 128 128 160 128"
            time.sleep(1.05)
            assert command("p") == "PAD 0 128 128 128 128"
            for query in ["buttons=4&lx=128&ly=128&rx=128&ry=128&ms=10", "buttons=0&lx=256&ly=128&rx=128&ry=128&ms=10",
                          "buttons=0&lx=128&ly=128&rx=128&ry=128&ms=2001", "buttons=0&lx=128&ly=128&rx=128&ry=128&ms=1&ms=2"]:
                assert request("/pad?" + query, "POST")[0] == 400
            assert request("/lease?seconds=3601", "POST")[0] == 400
            assert request("/lease?seconds=60", "POST")[0] == 204
            assert json.loads(request("/status")[2])["awake_seconds"] > 50
            code, headers, ppm = request("/screen")
            prefix = b"P6\n960 544\n255\n"
            assert code == 200 and ppm.startswith(prefix) and int(headers["X-Xita-Frame"]) > 0
            assert len(ppm) == len(prefix) + 960 * 544 * 3
            pixels = ppm[len(prefix):]
            for x, y in [(0, 0), (959, 543), (123, 234)]:
                offset = (y * 960 + x) * 3
                assert pixels[offset:offset+3] == bytes((x & 255, y & 255, 0x55))
            assert command("b") == "ACK"
            assert request("/screen")[0] == request("/log?offset=0")[0] == request('/launcher-log?offset=0')[0] == 409
            assert json.loads(request("/status")[2])["benchmark"] == 1
            assert request("/update")[0] == 409
            assert command("n") == "ACK"
            assert request('/benchmark?kind=unknown','POST')[0]==400
            assert request('/benchmark?kind=model-palette&kind=flare','POST')[0]==400
            assert request('/benchmark?kind=model-palette','POST',token='f'*32)[0]==403
            for kind in ('object-basis','model-palette','vertex-worker','vertex-references','native-bounds','vertex-copy','draw-scan','flare','resolution','early-visibility','point-math','texture-state','matrix-neon'):
                assert request('/benchmark?kind='+kind,'POST')[0]==204
                assert request('/benchmark?kind='+kind,'POST')[0]==409
                assert request('/screen')[0]==request('/update')[0]==409
                assert command('n')=='ACK'
            code, headers, body = request("/log?offset=0")
            assert code == 200 and body == log[:65536] and int(headers["X-Log-Size"]) == len(log)
            assert request("/log?offset=65536")[2] == log[65536:131072]
            assert request("/log?offset=" + str(len(log)))[2] == b""
            assert request("/log?offset=" + str(len(log)+1))[0] == 416
            assert request('/launcher-log?offset=0',token='f'*32)[0]==403
            assert request('/launcher-log?offset=0')[2]==launcher_log
            assert request('/launcher-log?offset=7')[2]==launcher_log[7:]
            assert request('/launcher-log?offset=0&path=remote.key')[0]==400
            assert request('/launcher-log?offset=0&offset=1')[0]==400
            assert command("f") == "ACK"
            assert request("/screen")[0] == 504
            assert command("f") == "ACK"
            assert request("/screen")[0] == 200  # timed-out ownership recovered
            pairing = tmp / "pair"
            subprocess.run(["python3", str(ROOT / "tools/vita_remote.py"), "pair", str(pairing),
                            "--host", "127.0.0.1", "--port", str(port)], check=True, stdout=subprocess.DEVNULL)
            conf = pairing / "remote-client.json"
            content = json.loads(conf.read_text()); content["key"] = key; conf.write_text(json.dumps(content))
            assert conf.stat().st_mode & 0o777 == 0o600
            client = Client(conf)
            assert client.status()["protocol"] == 1
            assert client.log(tmp / "client.log") == len(log)
            assert (tmp / "client.log").read_bytes() == log
            assert client.log(tmp/'launcher.log',launcher=True)==len(launcher_log)
            assert (tmp/'launcher.log').read_bytes()==launcher_log
            assert client.screen(tmp / "client.ppm")["frame"] > 0
            assert (tmp / "client.ppm").read_bytes() == ppm
            subprocess.run(["python3", str(ROOT / "tools/vita_remote.py"), "--config", str(conf),
                            "pad", "--rx", "160", "--duration", ".05"], check=True)
            assert command("p") == "PAD 0 128 128 128 128"
            assert command('h')=='ACK'  # ignored when no update is requested
            state=json.loads(request('/update')[2]);assert state['contract']==abi and state['state']==0 and state['handoff']==0
            manifest=f'/update/begin?size={len(new)}&sha256={hashlib.sha256(new).hexdigest()}&contract={abi}'
            assert request(manifest,'POST',token='f'*32)[0]==403
            assert request(manifest+'x','POST')[0]==409
            assert request(manifest,'POST')[0]==204
            assert request('/benchmark?kind=model-palette','POST')[0]==409
            assert request('/update/chunk?offset=1','POST',data=b'1234')[0]==409
            assert request('/update/chunk?offset=0','POST',data=bytes(65537))[0]==403
            assert request('/update/finish','POST')[0]==409
            # An abandoned chunk is never appended to the staged file.
            with socket.create_connection(('127.0.0.1',port),timeout=10) as sock:
                sock.sendall(b'POST /update/chunk?offset=0 HTTP/1.1\r\n'+auth+b'Content-Length: 64\r\n\r\nshort')
                sock.shutdown(socket.SHUT_WR)
                assert sock.recv(4096).split(b' ')[1]==b'409'
            assert json.loads(request('/update')[2])['received']==0
            result=upload_update(client,candidate,True)
            assert result['verified'] and result['restart_requested']
            assert json.loads(request('/update')[2])['handoff']==1
            assert command('h')=='ACK'
            state=json.loads(request('/update')[2]);assert state['handoff']==4 and state['requested']==1
            assert (app/'game-a.self').read_bytes()==old
            assert command('t')=='BOOT 1'
            assert (app/'game-a.self').read_bytes()==old and (app/'game-b.self').read_bytes()==new
            assert command('c')=='CONFIRM 0'
            assert command('t')=='BOOT 1'
        finally:
            proc.stdin.write(b"q"); proc.stdin.flush()
            assert proc.wait(timeout=10) == 0
        # Restart the production server on the same port immediately after
        # real traffic; updater handoffs must not lose their control endpoint.
        proc=subprocess.Popen([exe],cwd=tmp,env=dict(env,XV_REMOTE_TEST="1",XV_REMOTE_PORT=str(port)),stdin=subprocess.PIPE,stdout=subprocess.PIPE)
        try:
            assert proc.stdout.readline()==b"READY\n"
            assert request('/status')[0]==200
        finally:
            proc.stdin.write(b'q');proc.stdin.flush();assert proc.wait(timeout=10)==0
        print("PASS: real HTTP auth/framing, bounded controls/expiry/physical priority, leases, immutable frame colors, benchmark exclusion, log chunks and capture timeout recovery")


if __name__ == "__main__":
    main()
