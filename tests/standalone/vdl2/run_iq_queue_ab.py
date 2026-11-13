#!/usr/bin/env python3
"""Link the standalone IQ comparison using the existing OnAir test build.

This reuses the exact dependency flags of test_vdl2_rx; it never runs an app
or opens audio. ONAIR_BUILD must already have built that target.
"""
import os
from pathlib import Path
import shlex
import subprocess

here = Path(__file__).resolve().parent
root = here.parents[2]
onair = Path(os.environ.get("ONAIR_ROOT", "/Volumes/Projects/OnAir-reference-3.9.0"))
build = Path(os.environ.get("ONAIR_BUILD", root / "build/vdl2-audit/onair-build"))
out = root / "build/vdl2-audit"
out.mkdir(parents=True, exist_ok=True)
if not (build / "test_vdl2_rx").exists():
    raise SystemExit("Build OnAir's test_vdl2_rx target first")
cxx = os.environ.get("CXX", "c++")
objects = []
for source in (here / "iq_queue_ab.cpp", root / "src/core/AsyncIqDecoder.cpp"):
    obj = out / (source.stem + ".o")
    subprocess.run([cxx, "-std=c++17", "-O2", "-I", str(root / "src"),
                    "-I", str(onair / "core/include"), "-c", str(source),
                    "-o", str(obj)], check=True)
    objects.append(str(obj))
commands = subprocess.check_output(["ninja", "-t", "commands", "test_vdl2_rx"], cwd=build, text=True)
line = commands.splitlines()[-1]
args = shlex.split(line)
if args[:2] != [":", "&&"] or args[-2:] != ["&&", ":"]:
    raise SystemExit("Unexpected Ninja link command; refusing to execute it")
args = args[2:-2]
obj = "CMakeFiles/test_vdl2_rx.dir/tests/vdl2/test_vdl2_rx.cpp.o"
if obj not in args or "-o" not in args:
    raise SystemExit("Expected reference test object/output missing from link command")
i = args.index(obj)
args[i:i+1] = objects
args[args.index("-o")+1] = str(out / "iq_queue_ab")
subprocess.run(args, cwd=build, check=True)
env = dict(os.environ, DSDR_TEST_SILENT_AUDIO="1")
subprocess.run([str(out / "iq_queue_ab")], cwd=out, env=env, check=True)
