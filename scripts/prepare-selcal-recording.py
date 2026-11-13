#!/usr/bin/env python3
"""Prepare an external SELCAL audio demonstration as artificial AM IQ."""
import argparse
import array
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

HASH = "4a9d93f70bb04bc78c67ed0e3f17459b8f600fa1cddab0eabc318748d5898835"

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input",type=Path); parser.add_argument("output",type=Path)
    args=parser.parse_args(); metadata=args.output.with_suffix(".json")
    if args.output.suffix.lower()!=".wav" or hashlib.sha256(args.input.read_bytes()).hexdigest()!=HASH:
        parser.error("expected documented ICAO sample and .wav output")
    if args.output.exists() or metadata.exists(): parser.error("output already exists")
    try:
        raw=subprocess.run(["ffmpeg","-v","error","-i",str(args.input),"-t","14.5","-ac","1","-ar","192000","-f","f32le","-"],
                           capture_output=True,check=True).stdout
    except (OSError,subprocess.CalledProcessError) as error: parser.error(str(error))
    if not raw or len(raw)%4: parser.error("invalid converted audio")
    samples=array.array("f"); samples.frombytes(raw)
    if sys.byteorder!="little": samples.byteswap()
    size=len(samples)*8
    with args.output.open("xb") as out:
        out.write(b"RIFF"+struct.pack("<I",36+size)+b"WAVEfmt "+struct.pack("<IHHIIHH",16,3,2,192000,1536000,8,32)+b"data"+struct.pack("<I",size))
        for start in range(0,len(samples),8192):
            part=samples[start:start+8192]; iq=array.array("f",[0.0])*(len(part)*2)
            for i,value in enumerate(part): iq[2*i]=.55*(1+value)
            if sys.byteorder!="little": iq.byteswap()
            out.write(iq.tobytes())
    with metadata.open("x") as out: json.dump({"format":"decodium-iq/1","sampleRate":192000,"centerFrequencyHz":118000000},out)
    print("Prepared artificial AM IQ; no audio played")

if __name__=="__main__": main()
