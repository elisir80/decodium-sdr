#!/usr/bin/env python3
"""Turn the documented external pager audio into artificial FM IQ for tests.

The input is discriminator audio, not a native IQ recording. No audio plays.
"""
import argparse
import array
import hashlib
import json
import math
import struct
import wave
from pathlib import Path

HASH = "cb4c19e17fb8a58f57d09133fc2d7180b4b275fbf63171339f17f1823672cfbd"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, help="new .wav path; no overwriting")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".wav":
        parser.error("output must be a WAV path")
    if hashlib.sha256(args.input.read_bytes()).hexdigest() != HASH:
        parser.error("input SHA-256 does not match the documented recording")
    meta = args.output.with_suffix(".json")
    if args.output.exists() or meta.exists():
        parser.error("output WAV or metadata already exists")
    with wave.open(str(args.input), "rb") as src:
        if (src.getnchannels(), src.getsampwidth(), src.getframerate(), src.getcomptype()) != (1, 2, 48000, "NONE"):
            parser.error("expected 48 kHz mono PCM16")
        raw = src.readframes(src.getnframes())
    samples = [s[0] / 32768.0 for s in struct.iter_unpack("<h", raw)]
    if not samples:
        parser.error("empty input")
    frames = (len(samples) * 4 + 19200) * 8
    with args.output.open("xb") as out:
        out.write(b"RIFF" + struct.pack("<I", 36 + frames * 8) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 2, 192000, 1536000, 8, 32) + b"data" + struct.pack("<I", frames * 8))
        phase = 0.0
        for _ in range(8):
            for i, value in enumerate(samples):
                following = samples[min(i + 1, len(samples) - 1)]
                block = bytearray()
                for k in range(4):
                    phase += 2 * math.pi * 2000 * (value + (following - value) * k / 4) / 192000
                    block += struct.pack("<ff", 0.5 * math.cos(phase), 0.5 * math.sin(phase))
                out.write(block)
            out.write(struct.pack("<ff", 0.5, 0.0) * 19200)
    with meta.open("x") as out:
        json.dump({"format": "decodium-iq/1", "sampleRate": 192000, "centerFrequencyHz": 100000000}, out)
    print(f"Prepared {frames} IQ frames in {args.output}")


if __name__ == "__main__":
    main()
