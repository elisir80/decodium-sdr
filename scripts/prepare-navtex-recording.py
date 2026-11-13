#!/usr/bin/env python3
"""Prepare an optional WebSDR audio capture as artificial IQ for UI testing.

Requires ffmpeg. No audio is played. This is not a native RF/IQ capture.
"""
import argparse
import array
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

HASH = "a0b59dc6425625e5930bd768a56fc2c47e8863ee4139724966e92276a9ddd49d"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path)
    p.add_argument("output", type=Path, help="new .wav; existing files are not overwritten")
    args = p.parse_args()
    if args.output.suffix.lower() != ".wav":
        p.error("output must end in .wav")
    if hashlib.sha256(args.input.read_bytes()).hexdigest() != HASH:
        p.error("input SHA-256 does not match the documented recording")
    metadata = args.output.with_suffix(".json")
    if args.output.exists() or metadata.exists():
        p.error("output WAV or metadata already exists")
    # 70 seconds include SA97 and fit within a 32-bit RIFF length.
    try:
        result = subprocess.run(["ffmpeg", "-v", "error", "-i", str(args.input), "-t", "70", "-ac", "1", "-ar", "192000", "-f", "f32le", "-"],
                                check=True, capture_output=True)
    except (OSError, subprocess.CalledProcessError) as error:
        p.error(f"ffmpeg conversion failed: {error}")
    if not result.stdout or len(result.stdout) % 4:
        p.error("invalid ffmpeg output")
    samples = array.array("f")
    samples.frombytes(result.stdout)
    if sys.byteorder != "little":
        samples.byteswap()
    size = len(samples) * 8
    with args.output.open("xb") as out:
        out.write(b"RIFF" + struct.pack("<I", 36 + size) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 2, 192000, 1536000, 8, 32) + b"data" + struct.pack("<I", size))
        for start in range(0, len(samples), 8192):
            part = samples[start:start + 8192]
            iq = array.array("f", [0.0]) * (len(part) * 2)
            iq[::2] = part
            if sys.byteorder != "little":
                iq.byteswap()
            out.write(iq.tobytes())
    with metadata.open("x") as out:
        json.dump({"format": "decodium-iq/1", "sampleRate": 192000, "centerFrequencyHz": 518000}, out)
    print(f"Prepared {len(samples)} IQ frames in {args.output}")


if __name__ == "__main__":
    main()
