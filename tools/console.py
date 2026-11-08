#!/usr/bin/env python3
"""Send one or more console commands to the AirGuard board and print the reply.

    ~/.platformio/penv/bin/python tools/console.py status
    ~/.platformio/penv/bin/python tools/console.py diag --wait 8
    ~/.platformio/penv/bin/python tools/console.py "sim air 60" --port /dev/ttyUSB0

The port is opened WITHOUT pulsing reset (DTR/RTS untouched), so the board keeps
running. Duplicate lines are dropped (some USB-serial bridges repeat data when
the board's 3V3 rail is disturbed).
"""
import argparse
import glob
import re
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("commands", nargs="+")
    ap.add_argument("--port", default=None, help="default: first /dev/ttyUSB* or /dev/ttyACM*")
    ap.add_argument("--wait", type=float, default=3.0, help="seconds to collect the reply")
    args = ap.parse_args()

    port = args.port or next(iter(sorted(glob.glob("/dev/ttyUSB*") + glob.glob("/dev/ttyACM*"))), None)
    if not port:
        print("no serial port found (is the USB cable connected?)", file=sys.stderr)
        return 1

    p = serial.Serial(port, 115200, timeout=0.1)
    time.sleep(0.3)
    p.reset_input_buffer()
    p.write(b"\r\n")  # terminate any stray bytes already in the board's line buffer
    time.sleep(0.05)
    for c in args.commands:
        p.write((c + "\r\n").encode())
    t0, buf = time.time(), b""
    while time.time() - t0 < args.wait and len(buf) < 300_000:
        buf += p.read(4096)
    p.close()

    text = buf.decode(errors="replace").replace("\r", "")
    seen = set()
    # log lines start with "[  timestamp] LVL"; raw lines (diag, help) are kept as they are
    for line in re.split(r"\n|(?=\[\s*\d+\] [A-Z]{3} )", text):
        line = line.strip()
        if line and line not in seen:
            seen.add(line)
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
