# Firmware V0.6

- `GameBoy-CardputerADV-V0.6-app.bin`: aplicación para M5Launcher.
- `GameBoy-CardputerADV-V0.6-SD.zip`: aplicación, documentación y carpetas vacías.
- `SHA256SUMS`: integridad del BIN.

No contienen ROMs de arranque, cartuchos ni partidas. Copia tu `DMG.ROM` de
256 bytes a `/GAMEBOY/DMG.ROM` y tus `.gb` a `/GAMEBOY/GAMES/GB/`.

Compilación: `sh scripts/build.sh`. Paquete: `python3 scripts/package-sd.py`.
El BIN es de aplicación, no de flash completa. Consulta el estado de validación
del binario concreto en `BUILD-INFO.txt` y el alcance en
[VALIDACION.md](../VALIDACION.md).
