#!/usr/bin/env python3
"""Drive a headless Halo 2 harness run (games/halo2_5849/host) from the title screen into a split-screen match.

  tools/h2_menu_driver.py <run dir> [--timeout seconds] [--gap flips] [--hold flips]

Run the harness with XV_PAD_FILE=<run dir>/pad.txt and XV_HOST_STATUS=<run dir>/status.txt (tools/h2_host_run.sh
sets both when H2_DRIVE=1). The driver waits for each condition, writes the button into pad.txt, holds it for
--hold flips and clears it. Waits are counted in flips, not seconds, so the same plan works on x86 (~30 flips/s)
and on the Pi (a few flips/s). The menu remembers its highlights in the save tree (the lab's: profile "Default",
main menu on SPLIT SCREEN, lobby on START GAME), so the plan is the lab's Start, A, A, A, A:
  title    boot.log reports "[h2/menu-gxm] ready" (the first menu draw: the title is up and the game's idle
           counter starts; at 75 s of idle it plays the attract movie) and 240 more flips -> Start
  profile  -> A, main menu -> A, lobby profile page -> A, pregame lobby START GAME -> A  (--gap flips apart)
  prepare  START GAME may first prepare the map (cache copies) and return to the lobby: while no
           d:\\maps\\cyclotron.map open has been logged, press A again every 600 flips (at most 3 times)
  level    cyclotron.map opened, then [h2/perf] windows with >= 150 clears (the match clears ~3 times per frame):
           the driver logs 'LEVEL' and stops pressing.
Every step is logged with its flip to <run dir>/driver.log (and stdout). Exit 0 at LEVEL, 1 at timeout."""
import argparse, re, sys, time
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('run'); ap.add_argument('--timeout', type=float, default=7200)
    ap.add_argument('--gap', type=int, default=150); ap.add_argument('--hold', type=int, default=40)
    a = ap.parse_args()
    run = Path(a.run)
    pad, status = run / 'pad.txt', run / 'status.txt'
    boot, errlog = run / 'ux0:data/xita-halo2/boot.log', run / 'stderr.log'
    out = (run / 'driver.log').open('a')
    start = time.monotonic()
    pos = {'boot': 0, 'err': 0}
    seen = {'cyclotron': False, 'level': False, 'blocked': None, 'title': False}

    def log(msg):
        line = f'[driver] flip {flip()} t={time.monotonic() - start:.0f}s {msg}'
        print(line, flush=True); out.write(line + '\n'); out.flush()

    def flip():
        try: return int(status.read_text().split()[0])
        except (OSError, ValueError, IndexError): return 0

    def scan():
        for key, path in (('boot', boot), ('err', errlog)):
            try:
                with path.open('rb') as f:
                    f.seek(pos[key]); data = f.read(); pos[key] += len(data)
            except OSError: continue
            for line in data.decode('utf-8', 'replace').splitlines():
                if 'maps\\cyclotron.map' in line or 'maps/cyclotron.map' in line: seen['cyclotron'] = True
                if '[h2/blocked]' in line and not seen['blocked']: seen['blocked'] = line.strip()
                if '[h2/menu-gxm] ready' in line: seen['title'] = True
                m = re.search(r'\[h2/perf\].* clears=(\d+)', line)
                if m and seen['cyclotron'] and int(m.group(1)) >= 150: seen['level'] = True

    def wait(cond, what):
        while not cond():
            scan()
            if seen['blocked']: log(f'harness stopped: {seen["blocked"]}'); sys.exit(2)
            if seen['level']: return False
            if time.monotonic() - start > a.timeout: log(f'timeout waiting for {what}'); sys.exit(1)
            time.sleep(0.5)
        return True

    last_press = [0]

    def press(button, why):
        last_press[0] = flip()
        log(f'press {button} ({why})')
        pad.write_text(button + '\n')
        t0 = flip(); wait(lambda: flip() - t0 >= a.hold, f'{button} hold')
        pad.write_text('')

    def after(flips, what):
        t0 = flip(); return wait(lambda: flip() - t0 >= flips, what)

    pad.write_text('')
    log('waiting for the title ([h2/menu-gxm] ready)')
    if wait(lambda: seen['title'], 'title') and after(240, 'title animation'):
        press('start', 'title')
        for why in ('profile', 'main menu', 'lobby profile', 'START GAME'):
            if not after(a.gap, why): break
            press('a', why)
        for attempt in range(3):
            if not wait(lambda: seen['cyclotron'] or flip() - last_press[0] >= 600, 'map load'): break
            if seen['cyclotron']: break
            press('a', f'START GAME again #{attempt + 1} (no map load yet)')
    log('waiting for the level (cyclotron.map + in-match clears)')
    wait(lambda: seen['level'], 'level')
    log('LEVEL: in-match frames; driver done')


if __name__ == '__main__': main()
