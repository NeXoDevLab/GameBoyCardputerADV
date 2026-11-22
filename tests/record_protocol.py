#!/usr/bin/env python3
"""Offline checks for USB transport and corrupt/truncated recording packets."""
import importlib.util
import io
import os
from pathlib import Path
import struct
import threading
import unittest

root = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("record_device", root / "scripts/record-device.py")
recorder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recorder)


class Stream:
    def __init__(self, data):
        self.data = io.BytesIO(data)

    def read(self, size):
        data = self.data.read(size)
        if len(data) != size:
            raise EOFError("truncated fixture")
        return data


def packet(seq=0, magic=b"GBCR", samples=548, flags=3, size=5760):
    return struct.pack("<4sIHHI", magic, seq, samples, flags, size) + bytes(samples * 2 + size)


class Protocol(unittest.TestCase):
    def test_images(self):
        pixels = recorder.decode_pixels(bytes([0xe4]) * 5760)
        self.assertEqual(len(pixels), 160 * 144 * 3)
        self.assertEqual(pixels[:12], b"".join(recorder.RGB))
        with self.assertRaises(ValueError):
            recorder.decode_pixels(b"short")

    def test_video_audio_cadence(self):
        stream = Stream(packet() + packet(seq=1, flags=0, size=0))
        audio, picture, boot = recorder.read_packet(stream, 0)
        self.assertEqual((len(audio), len(picture), boot), (1096, 5760, True))
        audio, picture, boot = recorder.read_packet(stream, 1)
        self.assertEqual((len(audio), len(picture), boot), (1096, 0, False))

    def test_legacy_audio(self):
        pcm, packed, boot = recorder.read_packet(Stream(packet(samples=274)), 0, samples_per_frame=274)
        self.assertEqual((len(pcm), len(packed), boot), (548, 5760, True))

    def test_corruption(self):
        for kwargs in ({"magic": b"WRNG"}, {"seq": 1}, {"samples": 267},
                       {"flags": 8}, {"size": 12}, {"flags": 0}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                recorder.read_packet(Stream(packet(**kwargs)), 0)
        for cut in (1, 15, 16, 200, len(packet()) - 1):
            with self.subTest(cut=cut), self.assertRaises(EOFError):
                recorder.read_packet(Stream(packet()[:cut]), 0)

    def test_serial_link_on_pty(self):
        master, slave = os.openpty()
        try:
            with recorder.transport.Link(os.ttyname(slave), timeout=2) as link:
                link.send("?")
                self.assertEqual(os.read(master, 100), b"?\n")
                data = bytes(range(256)) * 100
                def transmit():
                    os.write(master, b"GBFRAME\r\n")
                    for at in range(0, len(data), 317):
                        os.write(master, data[at:at + 317])
                sender = threading.Thread(target=transmit)
                sender.start()
                self.assertEqual(link.line(), b"GBFRAME")
                self.assertEqual(link.read(len(data)), data)
                sender.join()
        finally:
            os.close(master)
            os.close(slave)


if __name__ == "__main__":
    unittest.main()
