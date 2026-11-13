#!/usr/bin/env python3
"""Compare product and pyModeS fields from the same decoded recorded frames."""
import math
import subprocess
import sys
from pathlib import Path
import pyModeS as pms

if len(sys.argv)!=3:
    raise SystemExit("usage: script PRODUCT_EXECUTABLE CRC_VALID_FRAME_FILE")
frames=Path(sys.argv[2]).read_bytes()
if not frames or len(frames)%14:
    raise SystemExit("bad frame file")
lines=subprocess.run([sys.argv[1],sys.argv[2]],check=True,capture_output=True,text=True).stdout.splitlines()
if len(lines)!=len(frames)//14:
    raise SystemExit("product output length mismatch")
compared=0
for n,line in enumerate(lines):
    fields=line.split(",")
    frame=frames[n*14:n*14+14].hex().upper()
    if pms.crc(frame)!=0 or int(fields[0])!=pms.adsb.typecode(frame):
        raise SystemExit("frame integrity/type mismatch")
    if int(fields[0])!=19:
        continue
    speed, direction, vertical, kind=pms.adsb.velocity(frame)
    if kind=="GS": actual_speed,actual_direction=float(fields[1]),float(fields[2])
    elif kind in ("IAS","TAS"):
        actual_speed,actual_direction=float(fields[4]),float(fields[5])
        if int(fields[6])!=int(kind=="TAS"): raise SystemExit("IAS/TAS mismatch")
    else: continue
    # pyModeS quantises speed to whole knots; our UI shows one decimal.
    if speed is not None and (not math.isfinite(actual_speed) or abs(actual_speed-speed)>=1.0): raise SystemExit("speed mismatch")
    if direction is not None and (not math.isfinite(actual_direction) or abs(actual_direction-direction)>0.01): raise SystemExit("direction mismatch")
    if vertical is not None and int(fields[3])!=vertical: raise SystemExit("vertical-rate mismatch")
    compared+=1
if compared==0: raise SystemExit("no comparable velocity frames")
print(f"PASS: {compared} recorded ADS-B velocity frames match pyModeS")
