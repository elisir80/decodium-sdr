#!/usr/bin/env python3
"""Prepare the optional acarsdec recording as artificial 1 MHz AM IQ.

This fixture is for receiver tests only: it is not a native RF/IQ recording.
The original recording is externally supplied and is never downloaded here.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

HASH = "752acabede3f96e8f137567e8218fe3cd9aab61130b9198d0cbce6cf2cf53b80"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path, help="new .wav path; existing files are not overwritten")
    args = parser.parse_args()
    if args.output.suffix.lower() != ".wav":
        parser.error("output must be a .wav file")
    raw = args.input.read_bytes()
    if hashlib.sha256(raw).hexdigest() != HASH:
        parser.error("input SHA-256 does not match the documented acarsdec recording")
    if raw[:4] != b"RIFF" or raw[8:12] != b"WAVE":
        parser.error("input is not a RIFF/WAVE recording")
    chunks = {}
    pos = 12
    while pos + 8 <= len(raw):
        name, size = struct.unpack_from("<4sI", raw, pos)
        pos += 8
        if pos + size > len(raw):
            parser.error("truncated WAV chunk")
        chunks[name] = raw[pos:pos + size]
        pos += size + (size & 1)
    fmt, data = chunks.get(b"fmt ", b""), chunks.get(b"data", b"")
    if len(fmt) < 40 or struct.unpack_from("<HHI", fmt) != (0xfffe, 4, 12500) or struct.unpack_from("<H", fmt, 14)[0] != 16 or struct.unpack_from("<H", fmt, 24)[0] != 1 or not data or len(data) % 8:
        parser.error("unexpected input format: expected four-channel 12.5 kHz PCM16")
    samples = [row[0] / 32768.0 for row in struct.iter_unpack("<hhhh", data)]
    frames = len(samples) * 80 + 200000
    size = frames * 8
    metadata = args.output.with_suffix(".json")
    if args.output.exists() or metadata.exists():
        parser.error("output WAV or metadata already exists")
    with args.output.open("xb") as out:
        out.write(b"RIFF" + struct.pack("<I", 36 + size) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 3, 2, 1000000, 8000000, 8, 32) + b"data" + struct.pack("<I", size))
        for i, value in enumerate(samples):
            following = samples[min(i + 1, len(samples) - 1)]
            out.write(b"".join(struct.pack("<ff", 0.5 + 0.45 * (value + (following-value)*k/80), 0.0) for k in range(80)))
        out.write(struct.pack("<ff", 0.5, 0.0) * 200000)
    with metadata.open("x") as out:
        json.dump({"format": "decodium-iq/1", "sampleRate": 1000000, "centerFrequencyHz": 131550000}, out)
    print(f"Prepared {frames} IQ frames in {args.output}")


if __name__ == "__main__":
    main()
