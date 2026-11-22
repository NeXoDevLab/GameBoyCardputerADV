#!/usr/bin/env python3
"""Install a Game Boy app through M5Launcher serial, using free space only.

Protocol reference: bmorcelli/Launcher src/serial_console.cpp and
tools/serial_flasher.py (2048-byte chunks with acknowledged byte counts).
Does not change existing partitions or use the overwrite command.
"""
import argparse
from hashlib import sha256
import importlib.util
import math
import os
from pathlib import Path
import re
import select
import time

spec = importlib.util.spec_from_file_location("serial_link", Path(__file__).with_name("serial-link.py"))
transport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transport)


def write_raw(link, data):
    deadline = time.monotonic() + 10
    while data:
        remaining = deadline - time.monotonic()
        if remaining <= 0 or not select.select([], [link.fd], [], remaining)[1]:
            raise TimeoutError("USB write timed out")
        try:
            data = data[os.write(link.fd, data):]
        except BlockingIOError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--file", type=Path, required=True)
    parser.add_argument("--name", default="GameBoy-CardputerADV-V0.6")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9_.-]+", args.name):
        parser.error("App name must not contain spaces or control characters")
    data = args.file.read_bytes()
    if not data or data[0] != 0xe9:
        parser.error("Expected an ESP32 application binary")
    print(f"Application bytes={len(data)} SHA256={sha256(data).hexdigest()}", flush=True)
    with transport.Link(args.port, timeout=15) as link:
        link.send("partitions")
        table = []
        for _ in range(200):
            line = link.line().decode("utf-8", errors="replace")
            table.append(line)
            print(line, flush=True)
            if "free total:" in line:
                break
        else:
            raise RuntimeError("No Launcher partition table received")
        free = re.findall(r"<free>\s+offset=0x([0-9a-fA-F]+) size=0x([0-9a-fA-F]+)", "\n".join(table))
        needed = math.ceil(len(data) / 65536) * 65536
        if not any(((int(at, 16) + 65535) // 65536 * 65536 + needed <= int(at, 16) + int(size, 16)) for at, size in free):
            raise RuntimeError("No contiguous free app slot; no installation was attempted")
        link.send(f"flash firmware {args.name} {len(data)}")
        for _ in range(100):
            line = link.line().decode("utf-8", errors="replace")
            print(line, flush=True)
            if line.startswith("ERR"):
                raise RuntimeError(line)
            if line == f"READY {len(data)}":
                break
        else:
            raise RuntimeError("Launcher did not accept the transfer")
        sent = acked = 0
        report = time.monotonic()
        while acked < len(data):
            if sent == acked:
                chunk = data[sent:sent + 2048]
                write_raw(link, chunk)
                sent += len(chunk)
            line = link.line().decode("utf-8", errors="replace")
            if line.startswith("ERR"):
                raise RuntimeError(line)
            match = re.fullmatch(r"ACK (\d+)/(\d+)", line)
            if match:
                value, total = map(int, match.groups())
                if total != len(data) or not acked < value <= sent:
                    raise RuntimeError(f"Invalid byte acknowledgement: {line}")
                acked = value
            if time.monotonic() - report >= 1 or acked == len(data):
                print(f"Acknowledged {acked}/{len(data)}", flush=True)
                report = time.monotonic()
        for _ in range(100):
            line = link.line().decode("utf-8", errors="replace")
            print(line, flush=True)
            if line.startswith("ERR"):
                raise RuntimeError(line)
            if "OK flashed, rebooting" in line:
                print("PASS: Launcher installed and selected the new app.", flush=True)
                return
        raise RuntimeError("No successful install confirmation")


if __name__ == "__main__":
    main()
