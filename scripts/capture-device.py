#!/usr/bin/env python3
"""Capture actual LCD pixels to PPM, without Pillow or pyserial."""
import argparse
import importlib.util
from pathlib import Path

spec = importlib.util.spec_from_file_location("serial_link", Path(__file__).with_name("serial-link.py"))
transport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transport)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with transport.Link(args.port) as link:
        link.send("c")
        for _ in range(100):
            if link.line() == b"GBFRAME 240 135 RGB565LE":
                break
        else:
            raise RuntimeError("No se recibio GBFRAME")
        raw = link.read(240 * 135 * 2)
        if link.line() != b"" or link.line() != b"GBFRAME END":
            raise RuntimeError("Captura USB incompleta")
    rgb = bytearray()
    for i in range(0, len(raw), 2):
        color = raw[i] | raw[i+1] << 8
        rgb.extend((((color >> 11) & 31) * 255 // 31, ((color >> 5) & 63) * 255 // 63, (color & 31) * 255 // 31))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b"P6\n240 135\n255\n" + rgb)
    print(args.output)


if __name__ == "__main__":
    main()
