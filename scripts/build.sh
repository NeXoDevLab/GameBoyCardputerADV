#!/bin/sh
set -eu
gb_root=$(cd "$(dirname "$0")/.." && pwd)
gb_cli=${GAMEBOY_ARDUINO_CLI:-arduino-cli}
if ! command -v "$gb_cli" >/dev/null 2>&1; then
  gb_cli='/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
fi
gb_fqbn='m5stack:esp32:m5stack_cardputer:PSRAM=disabled,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB,USBMode=hwcdc,CDCOnBoot=cdc'
python3 "$gb_root/scripts/prepare-dependencies.py"
"$gb_cli" compile --fqbn "$gb_fqbn" --build-path "$gb_root/.build-verified" \
  --library "$gb_root/.build-deps/M5Cardputer" --library "$gb_root/.build-deps/M5Unified" \
  --library "$gb_root/.build-deps/M5GFX" "$@" "$gb_root"
cp "$gb_root/.build-verified/GameBoyCardputerADV.ino.bin" "$gb_root/firmware/GameBoy-CardputerADV-V0.6-app.bin"
cd "$gb_root/firmware"
shasum -a 256 GameBoy-CardputerADV-V0.6-app.bin > SHA256SUMS
python3 "$gb_root/scripts/write-build-info.py"
