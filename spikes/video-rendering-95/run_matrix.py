#!/usr/bin/env python3
"""Throwaway renderer probe; never captures real media or connects to LiveKit."""
import argparse
import json
import statistics
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=10)
    parser.add_argument("--repeats", type=int, default=2)
    parser.add_argument("--gpu-metric", default="/sys/class/drm/card1/device/gpu_busy_percent")
    args = parser.parse_args()
    if args.seconds < 1 or args.repeats < 1:
        parser.error("seconds and repeats must be positive")
    results = []
    cases = [(count, 1280, 720) for count in (1, 3, 8, 10)] + [(10, 1920, 1080)]
    # Interleave modes to avoid running all CPU scaling trials cold and all
    # OpenGL scaling trials hot. Reverse their order on the second repetition.
    for repeat in range(args.repeats):
        modes = ["cpu-scale", "painter-scale"]
        if repeat % 2:
            modes.reverse()
        for streams, width, height in cases:
            for mode in modes:
                command = [str(args.binary.resolve()), "--streams", str(streams),
                           "--width", str(width), "--height", str(height),
                           "--fps", "30", "--seconds", str(args.seconds),
                           "--warmup", "2", "--mode", mode,
                           "--gpu-metric", args.gpu_metric]
                if repeat == 0 and streams == 10 and height == 720:
                    command += ["--screenshot", str(args.output.parent / f"{mode}-10-streams.png")]
                run = subprocess.run(command, text=True, capture_output=True,
                                     timeout=args.seconds + 30, check=True)
                if run.stderr.strip():
                    print(run.stderr.strip(), flush=True)
                result = json.loads(run.stdout)
                if not result["valid_rendering"]:
                    raise RuntimeError("No rendered frames; refusing invalid benchmark")
                if any("llvmpipe" in tile["renderer"].lower() or
                       "softpipe" in tile["renderer"].lower() for tile in result["tiles"]):
                    raise RuntimeError("Software OpenGL renderer; GPU benchmark invalid")
                result["repeat"] = repeat + 1
                results.append(result)
                args.output.write_text(json.dumps(results, indent=2) + "\n")
                fps = statistics.mean(tile["painted_fps"] for tile in result["tiles"])
                print(f"repeat={repeat + 1} streams={streams} {width}x{height} {mode}: "
                      f"CPU={result['process_cpu_percent_one_core']:.1f}% "
                      f"GPU-p50={result['gpu_busy_percent_systemwide_p50']:.0f}% "
                      f"FPS={fps:.2f} "
                      f"GUI-p95={result['gui_timer_delay_ms_p95']:.2f}ms", flush=True)


if __name__ == "__main__":
    main()
