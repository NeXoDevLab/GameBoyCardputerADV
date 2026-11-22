#!/usr/bin/env python3
"""Frame real USB captures with the same Cardputer ADV photo as Amstrad/Spectrum."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PHOTO = ROOT / 'docs/images/cardputer-adv-template.png'
MEDIA = ROOT / 'docs/media'
FFMPEG = shutil.which('ffmpeg')
CAPTURES = (
    ('boot-v06', 'docs/hardware-20261122/v06-2x-boot.png', 'V0.6'),
    ('superchase-v06', 'docs/media/superchase-v06.png', 'V0.6'),
    ('tetris-v03', 'docs/hardware-20261122/tetris-v03-live.png', 'V0.3'),
    ('yoshi-v04', 'docs/hardware-20261122/yoshi-v04.png', 'V0.4'),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def ffmpeg(*args):
    subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error', '-nostdin',
                    '-y', *map(str, args)], check=True)


def audio_hash(path):
    return subprocess.check_output([
        FFMPEG, '-hide_banner', '-loglevel', 'error', '-nostdin', '-i', str(path),
        '-map', '0:a:0', '-c:a', 'copy', '-f', 'hash', '-hash', 'sha256', '-'
    ]).decode().strip().split('=', 1)[1]


def main():
    if not FFMPEG:
        raise SystemExit('ffmpeg is required')
    if not PHOTO.exists():
        raise SystemExit(f'Missing template: {PHOTO}')
    # Same crop and native 240x135 LCD placement used in the other two projects.
    frame_filter = ('[1:v]crop=880:580:160:310[body];'
                    '[0:v]crop=240:135:0:0,setsar=1[screen];'
                    '[body][screen]overlay=231:54:shortest=1[video]')
    records = []
    for name, relative, version in CAPTURES:
        source = ROOT / relative
        target = MEDIA / f'{name}-cardputer-adv.png'
        ffmpeg('-i', source, '-i', PHOTO, '-filter_complex', frame_filter,
               '-map', '[video]', '-frames:v', '1', '-update', '1', target)
        records.append(dict(source=relative, source_sha256=sha(source),
                            firmware=version, output=target.name, sha256=sha(target)))

    source = MEDIA / 'gameplay-v06.mp4'
    target = MEDIA / 'gameplay-v06-cardputer-adv.mp4'
    # Source video has one padding row for H.264; crop it, preserve native pixels.
    ffmpeg('-i', source, '-loop', '1', '-framerate', '30', '-i', PHOTO,
           '-filter_complex', frame_filter, '-map', '[video]', '-map', '0:a:0',
           '-c:v', 'libx264', '-preset', 'fast', '-crf', '16', '-pix_fmt', 'yuv420p',
           '-c:a', 'copy', '-movflags', '+faststart', target)
    original_audio = audio_hash(source)
    if audio_hash(target) != original_audio:
        raise RuntimeError('Audio differs from the USB recording')
    gif = target.with_suffix('.gif')
    ffmpeg('-i', target, '-filter_complex',
           '[0:v]fps=20,split[a][b];[a]palettegen=stats_mode=diff[p];'
           '[b][p]paletteuse=dither=sierra2_4a:diff_mode=rectangle',
           '-loop', '0', gif)
    records.append(dict(source=source.name, source_sha256=sha(source),
                        firmware='V0.6', output=target.name, sha256=sha(target),
                        gif=gif.name, gif_sha256=sha(gif),
                        audio='Original AAC packets copied without re-encoding',
                        audio_packets_sha256=original_audio))

    cover = ROOT / 'firmware/GameBoy-CardputerADV-cover.png'
    ffmpeg('-i', MEDIA / 'superchase-v06-cardputer-adv.png',
           '-vf', 'pad=1280:720:(ow-iw)/2:(oh-ih)/2:white',
           '-frames:v', '1', '-update', '1', cover)
    metadata = dict(
        photograph='M5Stack product photograph reused from the Amstrad/Spectrum galleries',
        photograph_source='https://docs.m5stack.com/en/core/Cardputer-Adv',
        photograph_sha256=sha(PHOTO), canvas=[880, 580],
        screen_rect=[231, 54, 240, 135],
        note='USB emulation captures composited into a product photograph; not a photograph of a running screen.',
        outputs=records, cover=dict(path=str(cover.relative_to(ROOT)),
                                   canvas=[1280, 720], sha256=sha(cover)))
    (MEDIA / 'cardputer-gallery.json').write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2) + '\n')
    print('Built four device screenshots, the moving video/GIF and the M5Burner cover.')


if __name__ == '__main__':
    main()
