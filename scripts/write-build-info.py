#!/usr/bin/env python3
"""Record the firmware hash and confirm Arduino compiled the current sources."""
from datetime import datetime, timezone
from hashlib import sha256
from pathlib import Path
import json

root = Path(__file__).resolve().parent.parent
sources = [root / "GameBoyCardputerADV.ino"] + sorted((root / "src").rglob("*"))
lines = []
for source in sources:
    if not source.is_file():
        continue
    relative = source.relative_to(root)
    data = source.read_bytes()
    if source.suffix in (".c", ".cpp", ".h"):
        compiled = root / ".build-verified/sketch" / relative
        copy = compiled.read_bytes()
        if copy.startswith(b"#line 1 "):
            copy = copy.split(b"\n", 1)[1]
        if copy != data:
            raise SystemExit(f"El BIN no corresponde al codigo actual: {relative}")
    lines.append(f"{sha256(data).hexdigest()}  {relative}\n")
manifest = "".join(lines)
(root / "firmware/SOURCE-SHA256SUMS").write_text(manifest)
binary = root / "firmware/GameBoy-CardputerADV-V0.6-app.bin"
hardware="pending for this binary"
boot="pending for this binary"
report_path=root/"docs/hardware-20261122/validation.json"
if report_path.exists():
    report=json.loads(report_path.read_text())
    if (report.get("firmware_sha256")==sha256(binary.read_bytes()).hexdigest()
            and report.get("hardware_installed") and report.get("gameplay_verified_by_lcd")):
        hardware="passed installation, original boot and gameplay/I2S tests; see VALIDACION.md"
        if report.get("original_boot_verified"):
            boot="passed with original DMG ROM MD5 "+report["boot_rom_md5"]
info = f"""Game Boy DMG Cardputer ADV V0.6
Recorded (UTC): {datetime.now(timezone.utc).isoformat()}
Application bytes: {binary.stat().st_size}
Application SHA256: {sha256(binary.read_bytes()).hexdigest()}
Source manifest SHA256: {sha256(manifest.encode()).hexdigest()}
Board: m5stack:esp32:m5stack_cardputer
Core: M5Stack ESP32 3.3.10
Options: PSRAM=disabled, FlashMode=qio, FlashSize=8M, PartitionScheme=default_8MB, USBMode=hwcdc, CDCOnBoot=cdc
Libraries: M5Cardputer 1.1.1, M5Unified 0.2.28, M5GFX 0.2.35
ROMs and games: not included
Hardware validation: {hardware}
Original Nintendo boot ROM validation: {boot}
"""
(root / "firmware/BUILD-INFO.txt").write_text(info)
print(info)
