#!/usr/bin/env python3
"""Prepare documented discriminator audio as artificial AIS IQ for tests.

Requires ffmpeg. No audio is played; output is not native IQ or an RF capture.
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

HASH = "76db3dce818e4ec7be8c11b9d8baf93b80c03b94cfccfad52707d1098461d331"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path, help="new .wav path; no overwriting")
    args = p.parse_args()
    if args.output.suffix.lower() != ".wav": p.error("output must end in .wav")
    if hashlib.sha256(args.input.read_bytes()).hexdigest() != HASH: p.error("unexpected input SHA-256")
    meta = args.output.with_suffix(".json")
    if args.output.exists() or meta.exists(): p.error("output WAV or metadata already exists")
    try:
        result = subprocess.run(["ffmpeg", "-v", "error", "-i", str(args.input), "-t", "15", "-ac", "1", "-ar", "48000", "-f", "f32le", "-"], check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as error:
        p.error(f"ffmpeg conversion failed: {error}")
    if not result.stdout or len(result.stdout) % 4: p.error("invalid ffmpeg output")
    samples = array.array("f"); samples.frombytes(result.stdout)
    if sys.byteorder != "little": samples.byteswap()
    frames = len(samples) * 4
    with args.output.open("xb") as out:
        out.write(b"RIFF" + struct.pack("<I", 36 + frames * 8) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 2, 192000, 1536000, 8, 32) + b"data" + struct.pack("<I", frames * 8))
        phase = 0.0
        block = bytearray()
        for sample in samples:
            for _ in range(4):
                phase += 2 * math.pi * (-25000 + 2400 * sample) / 192000
                block += struct.pack("<ff", .5 * math.cos(phase), .5 * math.sin(phase))
            if len(block) >= 65536: out.write(block); block.clear()
        out.write(block)
    with meta.open("x") as out:
        json.dump({"format": "decodium-iq/1", "sampleRate": 192000, "centerFrequencyHz": 162000000}, out)
    print(f"Prepared {frames} IQ frames in {args.output}")


if __name__ == "__main__": main()
