# Guía de Game Boy DMG para Cardputer ADV

## Preparar microSD e instalar

Usa microSD FAT32. Extrae el ZIP de `firmware/` en la raíz conservando tus otras
carpetas. Copia tu ROM de encendido DMG de 256 bytes a `/GAMEBOY/DMG.ROM` y tus
cartuchos `.gb` a `/GAMEBOY/GAMES/GB/`.

Instala `GameBoy-CardputerADV-V0.6-app.bin` desde M5Launcher. Si compilas para una
primera instalación sin Launcher, utiliza Arduino CLI y el perfil del script
`scripts/build.sh`; las imágenes de bootloader/particiones están en
`.build-verified/`. El BIN de aplicación no se escribe a la dirección cero.
La instalación USB se ha verificado en Cardputer ADV, conservando Launcher,
Bruce y RFID. En el dispositivo de prueba sustituyó temporalmente Amstrad.

Al arrancar aparece el selector. Entra en `GB` con Enter, navega por las carpetas
y selecciona el cartucho. Al volver al selector, queda resaltado el último
cartucho; también se recuerda después de reiniciar. O elige el encendido original, activado por defecto.
R selecciona la entrada directa sin encendido. El modo se aplica al siguiente
cartucho/reinicio.

Si aparece «Falta /GAMEBOY/DMG.ROM (256 bytes)», comprueba la ruta y el tamaño.
Solo se comprueba el tamaño de esa ROM: un archivo de 256 bytes no garantiza
que sea una ROM DMG válida. Para cambiarla, reinicia el Cardputer después de
copiarla a la SD. El emulador la lee una sola vez al arrancar.

## Controles y partidas

Cruceta: flechas impresas sin Fn o WASD. A: Z/espacio. B: X. Start: Enter.
Select: Retroceso. Tab/G0 abre el selector. Fn+=/Fn+- ajusta el volumen.
Fn+P cambia verde/gris y Fn+W alterna proporción original/ancho completo.
Fn+B cambia el brillo. H en el selector muestra los controles.
Fn+R reinicia y Fn+S guarda la RAM de partida y los ajustes.

Volumen, brillo, verde/gris, anchura, modo de encendido y último cartucho quedan
en `/GAMEBOY/SETTINGS.INI`, con copia `.bak`. Se guardan al abrir/cerrar el
selector, cargar/reiniciar un cartucho o usar Fn+S. Para conservar un cambio de
ajuste antes de apagar, abre Tab. Si el archivo principal está truncado o sus
valores no son válidos, se intenta la copia y después los valores por defecto.

La partida se guarda junto al cartucho: `Juego.gb` → `Juego.sav`. Se conserva una
copia anterior `Juego.sav.bak`. Antes de sustituir el guardado se vuelve a leer
y comparar cada byte del temporal. Si el archivo principal falta o tiene tamaño
incorrecto, se intenta recuperar esa copia. Si existe una partida incompatible
y no hay copia válida, se impide arrancar el cartucho para evitar reemplazarla.
Conserva esos archivos antes de investigar cualquier error de guardado.

Un archivo `Juego.sav.tmp` puede quedar tras un guardado interrumpido. No se
restaura automáticamente: se prioriza el `.sav` confirmado y después el `.bak`.
Guarda mediante el menú del propio juego cuando corresponda y abre Tab antes
de apagar; el guardado del emulador conserva la RAM que haya escrito el juego.

## Diagnóstico y captura USB

No abras el monitor de Arduino y las herramientas Python sobre el mismo puerto
a la vez. Los comandos USB terminan en salto de línea y también funcionan en
el selector: cargar o grabar un cartucho lo cierra automáticamente.

| Comando | Acción |
| --- | --- |
| `?` | Estado, PC, frames, memoria, tiempos CPU/LCD/SD y contadores de audio |
| `boot` | Ruta, tamaño y MD5 de la ROM de arranque cargada |
| `files` | Listar cartuchos `.gb` en GAMES y subcarpetas |
| `exit` | Cerrar el selector por USB |
| `load /GAMEBOY/GAMES/GB/Juego.gb` | Cargar un cartucho |
| `r` | Reiniciar el cartucho |
| `s` | Guardar RAM |
| `m` | Abrir selector físico |
| `c` | Captura RGB565 de 240×135 |
| `record /GAMEBOY/GAMES/GB/Juego.gb` | Reiniciar con ROM original e iniciar vídeo/audio binario |
| `record-current` | Grabar la partida actual sin reiniciar |
| `key 8` | Pulsar Start durante seis fotogramas |
| `stop` | Finalizar grabación |
| `settings` | Ver ajustes y último cartucho recordado |
| `option volume 77` | Ajustar volumen, 0–255 |
| `option brightness 180` | Ajustar brillo, 32–255 |
| `option wide 0` / `option grey 0` / `option original 1` | Ajustes booleanos, 0/1 |
| `settings-save` | Guardar ajustes verificados en SD |
| `help` | Alternar ayuda si el selector está abierto |

Para instalar desde el ordenador mediante el protocolo serie de M5Launcher,
estando en Launcher y con una región libre suficiente:

```sh
python3 scripts/push-launcher.py --port /dev/cu.usbmodemXXXX \
  --file firmware/GameBoy-CardputerADV-V0.6-app.bin
```

La herramienta comprueba que haya espacio libre y usa `flash firmware`, que
crea una aplicación nueva. No borra ni sobrescribe otras aplicaciones. Si la
flash está llena, hay que elegir expresamente qué aplicación sustituir desde
Launcher o restaurar un firmware alternativo; no escribas en una dirección fija.

Los bits de `key` son: A=1, B=2, Select=4, Start=8, derecha=16, izquierda=32,
arriba=64 y abajo=128. Se pueden sumar. El teclado físico sigue funcionando.

Captura LCD con Python estándar, sin dependencias de Python adicionales:

```sh
python3 scripts/capture-device.py --port /dev/cu.usbmodemXXXX --output captura.ppm
```

Grabación para vídeo, con Python estándar y FFmpeg:

```sh
python3 scripts/record-device.py --port /dev/cu.usbmodemXXXX \
  --rom /GAMEBOY/GAMES/GB/Juego.gb --seconds 20 --start-at 7 --output encendido.mp4
```

Para capturar la partida abierta sin reiniciarla:

```sh
python3 scripts/record-device.py --port /dev/cu.usbmodemXXXX \
  --current --seconds 10 --output partida.mp4
```

Con `--rom`, la grabación fuerza la ROM de arranque original y comienza desde el primer
fotograma emulado. El audio se recoge del APU, sin añadir un sonido grabado. La
reproducción por el altavoz se pausa mientras se transmite por USB. La duración
se calcula con el reloj emulado; capturar 20 segundos puede tardar más de 20
segundos de reloj real.

El MP4 conserva la pantalla dentro de un lienzo de 240×136: la fila inferior
negra adicional permite codificación H.264 YUV420. También se conservan `.rgb`,
`.wav` y `.json` con datos de procedencia. No son una filmación del dispositivo;
son imagen y sonido obtenidos de la emulación en él. El protocolo USB se ha comprobado grabando el encendido de Tetris desde un
Cardputer ADV. Las capturas de V0.1 son anteriores a las mejoras de sonido de V0.6.

## Compilación y pruebas

Desde esta carpeta:

```sh
sh tests/run_host.sh
python3 tests/record_protocol.py
sh scripts/build.sh
python3 scripts/package-sd.py
```

El núcleo se prueba con código y cartuchos sintéticos propios. Esas pruebas
comprueban que se ejecuta el boot, que se desmapea mediante FF50, y que el
cartucho puede producir gráficos, sonido y escrituras en RAM. No comprueban
por sí solas que un volcado concreto de Nintendo funcione ni que un juego
comercial sea compatible.
