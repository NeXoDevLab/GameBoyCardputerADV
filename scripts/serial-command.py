#!/usr/bin/env python3
"""Send a newline-terminated diagnostic/Launcher command over USB."""
import argparse
import importlib.util
import os
from pathlib import Path
import select
import time

spec = importlib.util.spec_from_file_location("serial_link", Path(__file__).with_name("serial-link.py"))
transport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transport)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--command", required=True)
    parser.add_argument("--seconds", type=float, default=3)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    captured = bytearray()
    with transport.Link(args.port) as link:
        link.send(args.command)
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            if select.select([link.fd], [], [], min(.1, end - time.monotonic()))[0]:
                data = os.read(link.fd, 65536)
                if not data:
                    break
                captured.extend(data)
                print(data.decode("utf-8", errors="replace"), end="", flush=True)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(captured)

if __name__ == "__main__":
    main()
