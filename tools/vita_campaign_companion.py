#!/usr/bin/env python3
"""Enter the Halo CE campaign (New001 profile, continue Pillar of Autumn, Normal) using vitacompanion button presses
instead of the in-app pad endpoint, so it works even while the app's remote server is busy or dead.

  tools/vita_campaign_companion.py [--start dashboard|menu] [--host H]

dashboard: the app's dashboard has Halo CE selected and Launch Game highlighted (60 s for the game to boot to its
menu); menu: Halo's main menu with Campaign highlighted. Same steps as vita-campaign-sequence.py: cross x4/x5 with
waits. Navigation only, not proof of a successful load."""
import argparse, sys, time, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vita_companion import companion
def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--start', choices=('dashboard', 'menu'), default='dashboard'); ap.add_argument('--host', default='192.168.0.205'); a = ap.parse_args()
    steps = ([('Launch Halo CE', 60)] if a.start == 'dashboard' else []) + [('Campaign', 5), ('New001 profile', 5), ('Continue Pillar of Autumn', 5), ('Normal', 0)]
    for name, wait in steps:
        r = companion('press cross', a.host); time.sleep(0.3); companion('release cross', a.host)
        print(f'{name}: {r}', flush=True); time.sleep(wait)
    print('done'); return 0
if __name__ == '__main__': sys.exit(main())
