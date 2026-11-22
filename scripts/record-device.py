#!/usr/bin/env python3
"""Record emulated Game Boy boot, pictures and sound from a real Cardputer.

Standard Python library + ffmpeg. Captures an emulated duration, which may
take longer on USB. No overlays or fabricated boot animation are added.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import wave

spec = importlib.util.spec_from_file_location("serial_link", Path(__file__).with_name("serial-link.py"))
transport = importlib.util.module_from_spec(spec)
spec.loader.exec_module(transport)
PALETTE = (0x09c4, 0x32a5, 0x8c91, 0xc6d5)
RGB = tuple(bytes((((c >> 11) & 31) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)) for c in PALETTE)


def decode_pixels(packed):
    if len(packed) != 5760:
        raise ValueError("Fotograma GB incompleto")
    return b"".join(RGB[(byte >> shift) & 3] for byte in packed for shift in (0, 2, 4, 6))


def read_packet(link, sequence, samples_per_frame=548):
    header = link.read(16)
    magic, seq, samples, flags, video_bytes = struct.unpack("<4sIHHI", header)
    if magic != b"GBCR" or seq != sequence or samples != samples_per_frame or flags & ~3 or video_bytes not in (0, 5760):
        raise ValueError("Paquete USB invalido o fuera de secuencia")
    if bool(flags & 1) != bool(video_bytes) or bool(video_bytes) != (sequence % 2 == 0):
        raise ValueError("Cadencia de video USB incorrecta")
    audio = link.read(samples * 2)
    video = link.read(video_bytes) if video_bytes else b""
    return audio, video, bool(flags & 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--rom", help="/GAMEBOY/GAMES/GB/Nombre.gb")
    parser.add_argument("--current", action="store_true", help="Captura la partida actual sin reiniciar")
    parser.add_argument("--seconds", type=float, default=15)
    parser.add_argument("--output", type=Path, required=True, help="MP4 de salida")
    parser.add_argument("--start-at", type=float, help="Pulsa Start a estos segundos emulados")
    args = parser.parse_args()
    if not 0 < args.seconds <= 3600:
        parser.error("--seconds debe estar entre 0 y 3600")
    if args.current == bool(args.rom):
        parser.error("Usa --current o --rom, exclusivamente")
    if args.rom and (not args.rom.startswith("/GAMEBOY/GAMES/") or not args.rom.lower().endswith(".gb") or any(p in (".", "..") for p in args.rom.split("/")) or "\n" in args.rom or "\r" in args.rom):
        parser.error("Ruta GB no valida")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        parser.error("Instala ffmpeg para generar el MP4")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    raw_path = args.output.with_suffix(".rgb")
    wav_path = args.output.with_suffix(".wav")
    count = math.ceil(args.seconds * 4194304 / 70224)
    count += count % 2  # every exported video frame covers two emulated frames
    boot_frames = 0
    start_sent = False
    with transport.Link(args.port, timeout=15) as link, raw_path.open("wb") as video, wave.open(str(wav_path), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(32731)
        link.send("record-current" if args.current else "record " + args.rom)
        for _ in range(100):
            line = link.line()
            if line.startswith(b"GBREC1 "):
                fields = line.split()
                if len(fields) != 6 or fields[1:3] != [b"160", b"144"] or fields[4:] != [b"4194304", b"70224"] or fields[3] not in (b"16365", b"32731"):
                    raise ValueError("Cabecera GBREC no compatible")
                sample_rate = int(fields[3])
                samples_per_frame = 548 if sample_rate == 32731 else 274
                audio.setframerate(sample_rate)
                break
            print(line.decode("utf-8", errors="replace"))
        else:
            raise RuntimeError("No se inicio la grabacion original")
        try:
            for seq in range(count):
                pcm, packed, booting = read_packet(link, seq, samples_per_frame)
                boot_frames += booting
                audio.writeframesraw(pcm)
                if packed:
                    video.write(decode_pixels(packed))
                if args.start_at is not None and not start_sent and seq * 70224 / 4194304 >= args.start_at:
                    link.send("key 8")
                    start_sent = True
        finally:
            link.send("stop")
            # Drain the last in-flight packet before restoring the serial port.
            for seq in range(count, count + 120):
                if link.read(4) == b"GBRE":
                    if link.read(6) != b"C-END\r":
                        raise ValueError("Fin de grabacion no valido")
                    link.read(1)
                    break
                # A packet began while the stop command was in transit.
                tail = link.read(12)
                packet_seq, samples, flags, video_bytes = struct.unpack("<IHHI", tail)
                if packet_seq != seq or samples != samples_per_frame or video_bytes not in (0, 5760):
                    raise ValueError("Paquete final no valido")
                link.read(samples * 2 + video_bytes)
            else:
                raise RuntimeError("El dispositivo no termino la grabacion")
    subprocess.run([ffmpeg, "-y", "-v", "error", "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "160x144",
                    "-framerate", "4194304/140448", "-i", str(raw_path), "-i", str(wav_path),
                    "-vf", "scale=150:135:flags=area,pad=240:136:45:0:black,setsar=1", "-c:v", "libx264", "-crf", "15",
                    "-pix_fmt", "yuv420p", "-c:a", "aac", "-b:a", "128k", "-shortest", "-movflags", "+faststart", str(args.output)], check=True)
    args.output.with_suffix(".json").write_text(json.dumps({"source": "Cardputer ADV USB emulation", "rom": args.rom,
        "original_boot": None if args.current else True, "capture_current": args.current,
        "boot_frames": boot_frames, "cpu_frames": count, "clock": 4194304,
        "cycles_per_frame": 70224, "audio_sample_rate": sample_rate}, indent=2) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
