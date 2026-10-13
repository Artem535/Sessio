#!/usr/bin/env python3
"""Instrument the provider smoke's source files; Qt and SDK stay external.

Usage: script <configured-and-built-Ninja-build> <compatible-libcurl>
Requires CMAKE_EXPORT_COMPILE_COMMANDS=ON and clang++.
"""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

build = Path(sys.argv[1]).resolve()
curl = Path(sys.argv[2]).resolve()
output = build / "microphone-provider-sanitized"
output.mkdir(exist_ok=True)
entries = json.loads((build / "compile_commands.json").read_text())
entries = [entry for entry in entries
           if "Sessio_livekit_video_provider_smoke_test.dir/" in entry["command"]]
if not entries:
    raise RuntimeError("Configure/build Sessio_livekit_video_provider_smoke_test first")
compiler = os.environ.get("SESSIO_SANITIZER_CXX", "clang++")
sanitizers = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
objects = []
for index, entry in enumerate(entries):
    command = shlex.split(entry["command"])
    # This target contains ordinary translation units, no C++ modules. Drop
    # GCC's CMake module-scanner switches when compiling them with Clang.
    command = [arg for arg in command if not arg.startswith(
        ("-fmodules-ts", "-fmodule-mapper=", "-fdeps-format="))]
    obj = output / f"{index}.o"
    command[command.index("-o") + 1] = str(obj)
    subprocess.run([compiler, *sanitizers, *command[1:]],
                   cwd=entry["directory"], check=True)
    objects.append(str(obj))
sdk = next((build / "_deps/livekit-sdk").glob("*/lib/liblivekit.so"))
qt = shlex.split(subprocess.check_output(
    ["pkg-config", "--libs", "Qt6Core", "Qt6Widgets", "Qt6Multimedia",
     "Qt6OpenGLWidgets", "Qt6Network"], text=True))
binary = output / "provider"
subprocess.run([compiler, *sanitizers, *objects, "-o", str(binary), *qt,
                str(sdk), str(curl), f"-Wl,-rpath,{sdk.parent}"], check=True)
environment = os.environ.copy()
environment["ASAN_OPTIONS"] = "detect_leaks=0"
environment["LD_LIBRARY_PATH"] = str(curl.parent) + ":" + environment.get("LD_LIBRARY_PATH", "")
subprocess.run([str(binary), "--microphone-failure-only"],
               env=environment, check=True)
