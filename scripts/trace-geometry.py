#!/usr/bin/env python3
"""Read-only requested/observed geometry trace; never moves desktop windows."""
import argparse
import datetime
import json
import pathlib
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="output name or stable ID, e.g. DP-1")
    parser.add_argument("window", help="numeric window ID, UUID, or application selector such as app:firefox")
    parser.add_argument("--seconds", type=float, default=12)
    parser.add_argument("--interval", type=float, default=0.1)
    parser.add_argument("--socket", help="custom daemon socket, if used")
    args = parser.parse_args()
    if not 0 < args.seconds <= 120 or not 0.05 <= args.interval <= 5:
        parser.error("seconds must be in (0,120], interval in [0.05,5]")
    cli = pathlib.Path(__file__).resolve().parents[1] / "build" / "kwabai"
    command = [str(cli)] + (["--socket", args.socket] if args.socket else [])
    command += ["context", "geometry", args.output]
    print("Recording read-only geometry. Drag Firefox between tiles of different widths now.", file=sys.stderr)
    start = time.monotonic()
    previous = None
    matched = False
    try:
        while time.monotonic() - start < args.seconds:
            result = subprocess.run(command, capture_output=True, text=True, timeout=3)
            if result.returncode:
                print(result.stderr.strip(), file=sys.stderr)
                return result.returncode
            report = json.loads(result.stdout)
            def selected(window):
                if args.window.startswith("app:"):
                    name = args.window[4:].casefold()
                    app = window.get("app", "").casefold()
                    return bool(name) and (app == name or app.endswith("." + name))
                return args.window in (window.get("id"), window.get("uuid"))
            windows = [w for w in report["windows"] if selected(w)]
            if windows:
                matched = True
            sample = {k: report[k] for k in ("context-id", "bridge-instance", "transaction", "transaction-state", "stored-generation", "transaction-generation", "confirmed", "active-count", "plan-error", "pixel-aligned")}
            sample["windows"] = windows
            if sample != previous:
                previous = sample
                sample = {**sample, "elapsed-ms": round((time.monotonic() - start) * 1000), "time": datetime.datetime.now(datetime.timezone.utc).isoformat()}
                print(json.dumps(sample, separators=(",", ":")), flush=True)
            time.sleep(args.interval)
    except (OSError, subprocess.TimeoutExpired, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 1
    if not matched:
        print("Window was not found in this context. Check its current ID with query --windows.", file=sys.stderr)
        return 2
    print("Geometry trace complete.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
