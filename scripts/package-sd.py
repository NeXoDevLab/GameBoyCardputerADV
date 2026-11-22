#!/usr/bin/env python3
"""Package firmware and empty SD folders. No boot ROM, cartridges or saves."""
from hashlib import sha256
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

root = Path(__file__).resolve().parent.parent
binary = root / "firmware/GameBoy-CardputerADV-V0.6-app.bin"
output = root / "firmware/GameBoy-CardputerADV-V0.6-SD.zip"
digest = sha256(binary.read_bytes()).hexdigest()
(root / "firmware/SHA256SUMS").write_text(f"{digest}  {binary.name}\n")
instructions = """Game Boy DMG para Cardputer ADV - V0.6

1. Extrae este ZIP en la raiz de tu microSD FAT32.
2. Copia tu ROM de arranque DMG (256 bytes) como /GAMEBOY/DMG.ROM.
3. Copia tus cartuchos .gb en /GAMEBOY/GAMES/GB/ o sus subcarpetas.
4. Instala GameBoy-CardputerADV-V0.6-app.bin desde M5Launcher.

O: encendido original ejecutando DMG.ROM (predeterminado).
R: entrada directa al cartucho, sin la secuencia original.
Tab/G0: selector. Flechas sin Fn o WASD: direccion.
Z/espacio: A. X: B. Enter: Start. Retroceso: Select.
Fn+= / Fn+-: volumen. Fn+R: reinicio. Fn+S: guardar RAM.
Fn+W: proporcion original / ancho completo. Fn+P: verde / gris.
Fn+B: brillo. H en selector: ayuda. Recuerda ajustes y ultimo cartucho al guardar.

Las partidas de cartuchos con bateria se guardan junto al .gb como .sav.
Espera al guardado antes de apagar; Tab guarda y pausa la emulacion.
No incluye ROMs ni juegos. No sustituye tus carpetas CPC464/SPECTRUM.
V0.6: menor trabajo grafico, teclado a 60 Hz, JOYP corregido, ajustes persistentes
y guardados verificados byte a byte. Audio interno 2x con FIR y salida I2S continua.
Estado de las pruebas de este BIN: GAMEBOY-DOCS/firmware/BUILD-INFO.txt.
Consulta las pruebas y los limites en VALIDACION.md.
"""
with ZipFile(output, "w", ZIP_DEFLATED) as archive:
    for folder in ("GAMEBOY/", "GAMEBOY/GAMES/", "GAMEBOY/GAMES/GB/"):
        archive.writestr(folder, b"")
    archive.write(binary, binary.name)
    archive.write(root / "firmware/SHA256SUMS", "SHA256SUMS")
    archive.writestr("LEEME-GAMEBOY.txt", instructions)
    for file in ("README.md", "VALIDACION.md", "THIRD_PARTY_NOTICES.md", "LICENSE"):
        archive.write(root / file, "GAMEBOY-DOCS/" + file)
    archive.write(root / "docs/GUIA.md", "GAMEBOY-DOCS/docs/GUIA.md")
    for file in ("README.md", "BUILD-INFO.txt", "SOURCE-SHA256SUMS", "SHA256SUMS"):
        archive.write(root / "firmware" / file, "GAMEBOY-DOCS/firmware/" + file)
    for file in ("minigb_apu-LICENSE", "peanut_gb.h"):
        # The complete Peanut-GB MIT/SameBoy notice is at the top of its header.
        text = (root / "src/vendor" / file).read_text()
        if file.endswith(".h"):
            text = text[:text.index("#ifndef PEANUT_GB_H")]
        archive.writestr("GAMEBOY-DOCS/licenses/" + file + ".txt", text)
    for name in ("M5Unified", "M5GFX"):
        archive.write(root / ".build-deps" / name / "LICENSE", "GAMEBOY-DOCS/licenses/" + name + "-LICENSE.txt")
    archive.write(root / ".build-deps/M5GFX/src/lgfx/Fonts/efont/COPYRIGHT.txt", "GAMEBOY-DOCS/licenses/efont-COPYRIGHT.txt")
    license_text = (root / "LICENSE").read_text()
    archive.writestr("GAMEBOY-DOCS/licenses/M5Cardputer-LICENSE.txt", "MIT License\n\nCopyright (c) 2025 M5Stack Technology CO LTD\n\n" + license_text[license_text.index("Permission is hereby"):])
print(output)
