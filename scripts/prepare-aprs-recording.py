#!/usr/bin/env python3
"""Prepare the documented external APRS audio as artificial NFM IQ.

Requires ffmpeg. No audio is played. Output is not native IQ or an RF capture.
"""
import argparse
import array
import hashlib
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

HASH = "afd53b2c01227778aaca404cb8ba317302bf04b172e76a33359f15a2c13d56ab"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path, help="new .wav path; no overwriting")
    args = p.parse_args()
    if args.output.suffix.lower() != ".wav":
        p.error("output must end in .wav")
    if hashlib.sha256(args.input.read_bytes()).hexdigest() != HASH:
        p.error("input SHA-256 does not match the documented recording")
    meta = args.output.with_suffix(".json")
    if args.output.exists() or meta.exists():
        p.error("output WAV or metadata already exists")
    try:
        result = subprocess.run(["ffmpeg", "-v", "error", "-i", str(args.input), "-ac", "1", "-ar", "48000", "-f", "f32le", "-"], check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as error:
        p.error(f"ffmpeg conversion failed: {error}")
    if not result.stdout or len(result.stdout) % 4:
        p.error("invalid ffmpeg output")
    samples = array.array("f")
    samples.frombytes(result.stdout)
    if sys.byteorder != "little": samples.byteswap()
    frames = (len(samples) * 4 + 19200) * 12
    with args.output.open("xb") as out:
        out.write(b"RIFF" + struct.pack("<I", 36 + frames * 8) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 2, 192000, 1536000, 8, 32) + b"data" + struct.pack("<I", frames * 8))
        phase = 0.0
        for _ in range(12):
            block = bytearray()
            for sample in samples:
                for _ in range(4):
                    phase += 2 * math.pi * 3000 * sample / 192000
                    block += struct.pack("<ff", .7 * math.cos(phase), .7 * math.sin(phase))
            out.write(block)
            out.write(bytes(19200 * 8))
    with meta.open("x") as out:
        json.dump({"format": "decodium-iq/1", "sampleRate": 192000, "centerFrequencyHz": 144800000}, out)
    print(f"Prepared {frames} IQ frames in {args.output}")


if __name__ == "__main__":
    main()
