#!/usr/bin/env python3
"""Fire-and-forget campaign entry: the same button steps as vita-campaign-sequence.py, but each pad request is sent with
a short timeout and errors are ignored, because under the scene overlap the in-app remote executes requests yet its
replies do not reach the client (perf97 trace). Progress is observed through the plugin FTP log instead.
  tools/vita_campaign_fire.py --config <remote-client.json> [--start dashboard|menu]"""
import argparse, http.client, json, sys, time, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vita_remote import BUTTONS
def fire(cfg, key, path, method='POST', timeout=2.5):
    try:
        c = http.client.HTTPConnection(cfg['host'], cfg.get('port', 8080), timeout=timeout)
        c.request(method, path, headers={'Authorization': 'Bearer ' + key})
        try: c.getresponse().read()
        except Exception: pass
        c.close(); return True
    except Exception as e: return False
def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--config', required=True); ap.add_argument('--start', choices=('dashboard', 'menu'), default='menu'); a = ap.parse_args()
    cfg = json.load(open(a.config)); key = cfg['key'] if 'key' in cfg else open(os.path.join(os.path.dirname(a.config), cfg['key_file'])).read().strip()
    cross = BUTTONS['cross'] if isinstance(BUTTONS, dict) else 0
    steps = ([('Launch Halo CE', 60)] if a.start == 'dashboard' else []) + [('Campaign', 5), ('New001 profile', 5), ('Continue Pillar of Autumn', 5), ('Normal', 0)]
    for name, wait in steps:
        ok = fire(cfg, key, f'/pad?buttons={cross}&lx=128&ly=128&rx=128&ry=128&ms=400'); time.sleep(0.45)
        fire(cfg, key, '/pad?buttons=0&lx=128&ly=128&rx=128&ry=128&ms=0')
        print(f'{name}: sent={ok}', flush=True); time.sleep(wait)
    print('done')
if __name__ == '__main__': main()
