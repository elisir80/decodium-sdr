#!/usr/bin/env python3
"""Unpack the deterministic synthetic VDL2 test signal into the build tree."""
import gzip
import hashlib
import pathlib
import sys

source, target = map(pathlib.Path, sys.argv[1:3])
data = gzip.decompress(source.read_bytes())
expected = "185e74a09d00126494f195a7e8344a5b6b6d26fe0fdeb89ac6cc0b9e41c97ba6"
if hashlib.sha256(data).hexdigest() != expected:
    raise SystemExit("VDL2 synthetic fixture failed its SHA-256 check")
target.parent.mkdir(parents=True, exist_ok=True)
target.write_bytes(data)
