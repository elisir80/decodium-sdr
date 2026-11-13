#!/usr/bin/env python3
"""Translate an external DSC audio sample to artificial IQ for muted tests."""
import argparse
import array
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

HASH = "3e170987f154a0769fa22ca94c1fe45e6083de9f78c90c24420055edae5fe2e0"

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path)
    a = p.parse_args()
    if a.output.suffix.lower() != ".wav" or hashlib.sha256(a.input.read_bytes()).hexdigest() != HASH:
        p.error("expected the documented recording and a .wav output")
    metadata = a.output.with_suffix(".json")
    if a.output.exists() or metadata.exists():
        p.error("output WAV or metadata already exists")
    try:
        data = subprocess.run(["ffmpeg", "-v", "error", "-i", str(a.input),
            "-af", "pan=mono|c0=c0,afreqshift=shift=1200", "-ar", "192000", "-f", "f32le", "-"],
            check=True, capture_output=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        p.error(str(e))
    if not data or len(data) % 4:
        p.error("invalid converted audio")
    samples = array.array("f"); samples.frombytes(data)
    if sys.byteorder != "little": samples.byteswap()
    size = len(samples)*8
    with a.output.open("xb") as out:
        out.write(b"RIFF" + struct.pack("<I",36+size) + b"WAVEfmt " + struct.pack("<IHHIIHH",16,3,2,192000,1536000,8,32) + b"data" + struct.pack("<I",size))
        for start in range(0,len(samples),8192):
            part=samples[start:start+8192]
            iq=array.array("f",[0.0])*(len(part)*2); iq[::2]=part
            if sys.byteorder != "little": iq.byteswap()
            out.write(iq.tobytes())
    with metadata.open("x") as out:
        json.dump({"format":"decodium-iq/1","sampleRate":192000,"centerFrequencyHz":8414500},out)
    print("Prepared artificial IQ; no audio played")

if __name__ == "__main__": main()
