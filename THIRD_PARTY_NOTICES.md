# Créditos y licencias

El código de integración, interfaz, caché, escalado y herramientas del proyecto
es MIT, Copyright (c) 2026 NeXoDevLab; véase [LICENSE](LICENSE).

## Peanut-GB

- Fuente: https://github.com/deltabeard/Peanut-GB
- Commit: `d0bcca771c83a2638c93a9ae61f3d51f226dc905`.
- Archivo `src/vendor/peanut_gb.h`.
- MIT, Copyright (c) 2018–2023 Mahyar Koshkouei.
- Incluye fragmentos procedentes de SameBoy: Copyright (c) 2015–2019
  Lior Halphon, MIT. El aviso completo se conserva en la cabecera del archivo.
- `peanut_core.c` es nuestro adaptador de compilación C99; no contiene ROMs.
- Parche local: ceder al llegar a un fotograma durante HALT, incluso con LCD
  apagada y sin interrupciones, y conservar HALT al reanudar sin interrupción.
  Evita bloquear la interfaz y ejecutar instrucciones después de un HALT permanente.
  Contador adicional de ciclos para sincronizar los accesos al APU.
  Lectura JOYP con selección de ambas filas, bits inferiores de solo lectura,
  actualización de botones y petición de interrupción al bajar una entrada.

## MiniGB APU

- Fuente: https://github.com/deltabeard/minigb_apu
- Commit: `344d9814c0d834faf1a26ec97b57bedddefed8aa`.
- Archivos `src/vendor/minigb_apu.c` y `.h`.
- MIT, Copyright (c) 2017 Alex Baines y (c) 2019 Mahyar Koshkouei.
- Aviso de licencia: `src/vendor/minigb_apu-LICENSE`.
- Cambios locales: configuración compartida, número entero de muestras por
  fotograma, acumulación ancha de los cuatro canales con margen de mezcla,
  normalización del DAC de onda, corrección del volumen NR32 aplicado dos veces,
  síntesis parcial, promediado de transiciones antes de dividir y número explícito
  de muestras internas para el sobremuestreo 2x. El filtro FIR es código de
  integración del proyecto.
- El adaptador conserva el estado del filtro de continua entre paquetes y
  transmite PCM directamente mediante el controlador I2S de ESP-IDF.

## Bibliotecas de plataforma

M5Cardputer 1.1.1, M5Unified 0.2.28 y M5GFX 0.2.35 se descargan desde los
repositorios oficiales de M5Stack a la carpeta ignorada `.build-deps/`. Se
conservan sus licencias y un registro de URL/SHA256. Arduino-ESP32 y ESP-IDF
se utilizan desde el entorno Arduino, con sus respectivas licencias.

## Referencias técnicas

- https://gbdev.io/pandocs/Power_Up_Sequence.html
- https://gbdev.io/pandocs/Joypad_Input.html
- https://github.com/deltabeard/Peanut-GB/blob/master/README.md
- https://github.com/deltabeard/minigb_apu/blob/master/README.md

Game Boy y Nintendo son marcas de sus titulares. Este proyecto no está afiliado
a Nintendo. La ROM de arranque y los cartuchos los aporta el usuario; no se
distribuyen ROMs ni datos extraídos de cartuchos.

Las pruebas de `tests/` utilizan programas y patrones sintéticos propios. Los
archivos de `docs/hardware-20261122/` incluyen capturas breves de imagen y audio
generados por la emulación durante las comprobaciones en el Cardputer. No son
una filmación del dispositivo ni una grabación acústica de su altavoz. El código
MIT del proyecto no concede derechos sobre el contenido de los juegos mostrado
en esas capturas.
