#!/usr/bin/env python3
"""Compare the adapter with the locally built OnAir C++ reference, no audio."""
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
objects = []
for source in (here / "frame_reference_ab.cpp", root / "src/dsp/vdl2/Vdl2Frame.cpp",
               root / "src/dsp/vdl2/Vdl2Burst.cpp", root / "src/dsp/vdl2/Vdl2Symbols.cpp",
               root / "src/dsp/vdl2/Vdl2Acquisition.cpp", root / "src/dsp/vdl2/Vdl2Offline.cpp",
               root / "src/dsp/vdl2/Vdl2WindowStream.cpp", root / "src/core/Vdl2AsyncStream.cpp",
               root / "src/core/AsyncIqDecoder.cpp", root / "src/core/Vdl2IqPipeline.cpp",
               root / "src/dsp/vdl2/Vdl2Frontend.cpp", root / "src/core/Vdl2ResultQueue.cpp",
               root / "src/core/Vdl2Session.cpp"):
    obj = out / (source.stem + ".o")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-O2",
                    "-I", str(root / "src"), "-I", str(onair / "core/include"),
                    "-c", str(source), "-o", str(obj)], check=True)
    objects.append(str(obj))
commands = subprocess.check_output(["ninja", "-t", "commands", "test_vdl2_rx"], cwd=build, text=True)
args = shlex.split(commands.splitlines()[-1])
if args[:2] != [":", "&&"] or args[-2:] != ["&&", ":"]:
    raise SystemExit("Unexpected Ninja link command")
args = args[2:-2]
test_obj = "CMakeFiles/test_vdl2_rx.dir/tests/vdl2/test_vdl2_rx.cpp.o"
if test_obj not in args or "-o" not in args:
    raise SystemExit("Expected reference test object/output missing")
i = args.index(test_obj)
args[i:i+1] = objects
args[args.index("-o") + 1] = str(out / "frame_reference_ab")
subprocess.run(args, cwd=build, check=True)
subprocess.run([str(out / "frame_reference_ab")], cwd=out,
               env=dict(os.environ, DSDR_TEST_SILENT_AUDIO="1"), check=True)
