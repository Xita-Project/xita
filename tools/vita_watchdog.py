#!/usr/bin/env python3
"""Unattended Xita runs: keep the app alive without anyone at the Vita.

  tools/vita_watchdog.py --config <remote-client.json> [--interval 20] [--dead-after 90] [--max-relaunches 20]
                         [--env K=V ...] [--log-dir DIR] [--reboot-after 3]

Loop: poll the in-app remote /status. When it has not answered for --dead-after seconds, the app is gone
(crash or hang): pull nothing (the log is already on the device), press cross twice through vitacompanion to
dismiss the "quit unexpectedly" dialog, re-arm the process-only env (vita_remote.py env) and launch XITA00001.
--reboot-after consecutive failed relaunches reboot the console (vitacompanion) and try again. Every event is
appended to <log-dir>/watchdog.log with a timestamp; the count of relaunches is the honest crash rate of a build.
Hung-but-alive apps (status answers, frames stop) are the app's own watchdog's job."""
import argparse, json, os, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vita_companion import companion
def status(cfg):
    try:
        out = subprocess.run([sys.executable, os.path.join(HERE, 'vita_remote.py'), '--config', cfg, 'status'], capture_output=True, text=True, timeout=10).stdout
        return json.loads(out) if out.strip().startswith('{') else None
    except Exception: return None
def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--config', required=True); ap.add_argument('--interval', type=float, default=20)
    ap.add_argument('--dead-after', type=float, default=90); ap.add_argument('--max-relaunches', type=int, default=20); ap.add_argument('--reboot-after', type=int, default=3)
    ap.add_argument('--env', nargs='*', default=[]); ap.add_argument('--campaign', default=None, help='vita-campaign-sequence.py path: re-enter the campaign after a relaunch'); ap.add_argument('--log-dir', default='.'); ap.add_argument('--host', default='192.168.0.205'); a = ap.parse_args()
    os.makedirs(a.log_dir, exist_ok=True); logf = open(os.path.join(a.log_dir, 'watchdog.log'), 'a')
    def log(m): line = time.strftime('%Y-%m-%d %H:%M:%S ') + m; print(line, flush=True); logf.write(line + '\n'); logf.flush()
    log(f'watchdog start: interval {a.interval}s dead-after {a.dead_after}s env {a.env}')
    try: log('nosleep: ' + companion('nosleep on', a.host))
    except Exception as e: log(f'nosleep failed: {e}')
    last_ok = time.time(); relaunches = 0; failed_in_a_row = 0; last_frame = None
    while relaunches <= a.max_relaunches:
        st = status(a.config)
        if st:
            last_ok = time.time(); failed_in_a_row = 0
            fr = st.get('frame') or st.get('frames'); 
            if fr != last_frame: last_frame = fr
        elif time.time() - last_ok > a.dead_after:
            relaunches += 1; failed_in_a_row += 1
            log(f'app unreachable for {int(time.time() - last_ok)}s: relaunch #{relaunches} (failed in a row {failed_in_a_row})')
            try:
                if failed_in_a_row > a.reboot_after: log('reboot: ' + companion('reboot', a.host)); time.sleep(90)
                companion('press cross', a.host); time.sleep(1); companion('press cross', a.host); time.sleep(1)
                companion('quit all', a.host); time.sleep(5)   # a launch straight after a crash did not reach the app's server; quit all first works
                log('launch: ' + companion('launch XITA00001', a.host))
                # the in-app remote must be up before /env (process-only, applies to the next Launch Game); wait for it
                up = None
                for _ in range(24):
                    time.sleep(5); up = status(a.config)
                    if up: break
                if not up: raise RuntimeError('app did not come up after launch')
                if a.env: log('env: ' + subprocess.run([sys.executable, os.path.join(HERE, 'vita_remote.py'), '--config', a.config, 'env'] + a.env, capture_output=True, text=True, timeout=20).stdout.strip()[:200])
                if a.campaign: log('campaign: rc %d' % subprocess.run([sys.executable, a.campaign, '--expected-version', up.get('version', ''), '--out', os.path.join(a.log_dir, 'relaunch-%d' % relaunches)], capture_output=True, text=True, timeout=400).returncode)
            except Exception as e: log(f'relaunch error: {e}')
            last_ok = time.time()   # give the boot dead_after seconds before judging again
        time.sleep(a.interval)
    log(f'giving up after {relaunches} relaunches')
if __name__ == '__main__': main()
