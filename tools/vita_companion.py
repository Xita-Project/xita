#!/usr/bin/env python3
"""Client for the vitacompanion plugin (github.com/devnoname120/vitacompanion, 1.07) on the Vita: a command
server in the shell that answers whether or not Xita is running.

  tools/vita_companion.py [--host H] <command...>      e.g. launch XITA00001 | quit all | reboot | press cross
                                                             nosleep on|off|status | screen on|off | version | wait 2s
The in-app remote (tools/vita_remote.py) is the control plane while Xita runs; this is the plane that survives a
crash. Installed 2026-09-21 (ur0:tai/vitacompanion*.skprx/.suprx, config lines under *KERNEL and *main)."""
import socket, sys
HOST = '192.168.0.205'; PORT = 1338
def companion(command, host=HOST, timeout=8.0):
    s = socket.create_connection((host, PORT), timeout=timeout); s.sendall((command + '\n').encode()); s.settimeout(timeout); out = b''
    try:
        while True:
            d = s.recv(4096)
            if not d: break
            out += d
            if out.endswith(b'\n'): break
    except socket.timeout: pass
    s.close(); return out.decode(errors='replace').strip()
def main():
    a = sys.argv[1:]; host = HOST
    if a[:1] == ['--host']: host = a[1]; a = a[2:]
    if not a: print(__doc__); return 2
    print(companion(' '.join(a), host)); return 0
if __name__ == '__main__': sys.exit(main())
