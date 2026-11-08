#!/usr/bin/env python3
"""Log MQ-135 AO voltage, alarm index and severities once per second.

    ~/.platformio/penv/bin/python tools/trace_air.py 120 > logs/air_trace.txt
"""
import re
import sys
import time

import serial

dur = float(sys.argv[1]) if len(sys.argv) > 1 else 120
p = serial.Serial("/dev/ttyUSB0", 115200, timeout=0.05)
time.sleep(0.3)
p.reset_input_buffer()
p.write(b"\r\n")
t0 = time.time()
last = 0.0
buf = ""
print("t_s  Vao_V  idx   sev(AIR/SYS overall)  events", flush=True)
row = {}
while time.time() - t0 < dur:
    if time.time() - last >= 1.0:
        p.write(b"status\r\n")
        last = time.time()
    buf += p.read(4096).decode(errors="replace")
    while "\n" in buf:
        line, buf = buf.split("\n", 1)
        m = re.search(r"Vao=([\d.]+)V.*idx=([\d.]+)", line)
        if m:
            row = {"v": m.group(1), "i": m.group(2)}
        m = re.search(r"sev AIR=(\w+).*?SYS=(\w+).*buzzer=(\w+)", line)
        if m and row:
            print(f"{time.time()-t0:5.1f}  {row['v']:>5}  {row['i']:>5}  AIR={m.group(1):6} buzzer={m.group(3)}", flush=True)
            row = {}
        if "EVENT" in line:
            print(f"{time.time()-t0:5.1f}  >>> {line.strip()}", flush=True)
