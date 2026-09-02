#!/usr/bin/env python3
"""Minimal X11 input injection via XTest (ctypes, no extra packages).
   xtest_click.py click X Y        left-click at screen coords
   xtest_click.py key  KEYSYM      press+release a key (e.g. Return, Escape)
"""
import ctypes, ctypes.util, sys, time

x11 = ctypes.CDLL(ctypes.util.find_library("X11"))
xtst = ctypes.CDLL(ctypes.util.find_library("Xtst"))
x11.XOpenDisplay.restype = ctypes.c_void_p
x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
x11.XFlush.argtypes = [ctypes.c_void_p]
x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
x11.XStringToKeysym.restype = ctypes.c_ulong
x11.XStringToKeysym.argtypes = [ctypes.c_char_p]
x11.XKeysymToKeycode.restype = ctypes.c_ubyte
x11.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
xtst.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
xtst.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]

d = x11.XOpenDisplay(None)
if not d:
    sys.exit("cannot open display")
cmd = sys.argv[1]
if cmd == "click":
    x, y = int(sys.argv[2]), int(sys.argv[3])
    xtst.XTestFakeMotionEvent(d, -1, x, y, 0); x11.XFlush(d); time.sleep(0.1)
    xtst.XTestFakeButtonEvent(d, 1, 1, 0); x11.XFlush(d); time.sleep(0.08)
    xtst.XTestFakeButtonEvent(d, 1, 0, 0); x11.XFlush(d)
elif cmd == "key":
    kc = x11.XKeysymToKeycode(d, x11.XStringToKeysym(sys.argv[2].encode()))
    xtst.XTestFakeKeyEvent(d, kc, 1, 0); x11.XFlush(d); time.sleep(0.08)
    xtst.XTestFakeKeyEvent(d, kc, 0, 0); x11.XFlush(d)
x11.XCloseDisplay(d)
print("ok", cmd)
