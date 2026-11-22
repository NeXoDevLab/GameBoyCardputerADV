# Validación V0.6 — 22 de noviembre de 2026

La revisión final V0.6 con síntesis 2x/FIR está instalada en el Cardputer ADV,
con verificación del hash de escritura. Se comprobó la ROM DMG original,
captura del logo y entrada a una carrera de Super Chase H.Q. Las pruebas de
las mejoras generales de la primera compilación se conservan por separado en
`validation-v06-initial.json`; el resultado del binario final está en
`validation.json`. El usuario confirma que ahora se oye mejor, sin el ruido
molesto, durante la carrera de Super Chase H.Q.

## Mejoras generales V0.6

La CPU emula cada fotograma y el APU conserva la sincronización de V0.5. Para
la pantalla nativa, solo se construyen los píxeles de los fotogramas que se van
a mostrar (unos 30 fps). Se conserva el flujo de grabación USB. La prueba de
comparación ejecuta la misma ROM sintética con y sin esta reducción: PC, flags,
RAM y audio coinciden en todos los fotogramas y los píxeles coinciden en cada
fotograma presentado. El sondeo de teclado pasa de 33 a 16 ms.

JOYP ahora combina ambas filas si están seleccionadas, respeta los bits de
solo lectura y refleja los cambios de botones aunque el juego no vuelva a
escribir FF00. Solicita la interrupción al bajar una entrada seleccionada.
Se verificaron 1.024 combinaciones de selección/botones, las transiciones de
entrada y una ROM sintética que despierta de HALT mediante esa interrupción.
No se afirma compatibilidad con todos los cartuchos a partir de estas pruebas.

Los ajustes de volumen, brillo, paleta, anchura, modo de encendido y último
cartucho se guardan en `/GAMEBOY/SETTINGS.INI`. La recuperación tras un reinicio
real se comprobó cambiando volumen a 104, brillo a 128 y ambos modos de imagen
a 1, verificando que se recuperaban exactamente. Después se restauraron los
valores habituales: 77, 180, imagen proporcional, paleta verde y arranque
original. Otro reinicio confirmó el último cartucho recordado. Registros:
`settings-v06-before-reset.txt`, `settings-v06-after-reset.txt` y
`v06-lastgame-boot.txt`. `v06-boot.png` muestra la ROM original ejecutándose.

Fn+B cambia brillo; H abre/cierra la ayuda del selector. El selector resalta el
último cartucho. Los ajustes se escriben en pausas explícitas para evitar
escrituras continuas durante la partida.

El guardado conserva el formato `.sav` y ahora vuelve a leer y comparar todos
los bytes del temporal antes de sustituir el principal. La misma transacción
protege los ajustes. Se probaron corrupción de igual longitud, fallo de
escritura, eliminación y ambas fases de renombrado, con recuperación y reintento
manteniendo la copia anterior. Son pruebas con almacenamiento simulado: no
sustituyen una prueba exhaustiva de cortes eléctricos sobre FAT32 real.

### Ruido restante y filtro 2x

El usuario indicó ruido durante la partida con la primera compilación V0.6,
aunque no se registraron vaciados de audio. La revisión posterior conserva las
mejoras generales y genera 1.096 muestras internas por fotograma (~65,5 kHz).
Un FIR de 31 coeficientes promedia el historial antes de reducir a las 548
muestras de salida. Su estado se conserva entre fotogramas y solo se reinicia
con el cartucho. No se silencian canales ni efectos para ocultar el ruido.

Prueba sintética del filtro: nivel conservado a 1 kHz, rechazo superior a
47 dB para tonos de 20, 22 y 25 kHz que se plegarían al reducir, ganancia unitaria
en continua y reset. Las comparaciones CPU/RAM/audio del renderizado alterno
se repitieron con el nuevo filtro y pasan con sanitizadores. Esto verifica el
filtro, no la fidelidad de todos los efectos del núcleo.

Prueba del binario final: carrera con aceleración, 48,758 segundos entre
lecturas y **59,724 fotogramas/s**. La cola permaneció en cuatro paquetes y los
contadores de vaciados, pérdidas, errores I2S y expiraciones fueron **cero**.
No hubo nuevas lecturas SD durante la observación. Registros:
`superchase-v06-2x-live.txt` y `superchase-v06-2x-summary.json`.
El usuario confirma: «Se oye mejor, sin el ruido molesto». Esta confirmación
corresponde a Super Chase H.Q.; no implica fidelidad perfecta en todos los juegos.

## Resultado en Cardputer ADV real

Se instaló Game Boy en la partición que ocupaba Amstrad, a `0x6f0000`, con
sustitución expresamente autorizada por el usuario. Launcher, Bruce, RFID y las
otras particiones conservan sus direcciones y tamaños. La escritura de la
aplicación y los metadatos de arranque terminó con verificación de hash.

Antes de instalar se guardó una copia completa de la flash de 8 MB y se extrajo
Amstrad FIX12. Su SHA256 coincide con el firmware original del proyecto. Las
copias quedan en `.device-backups/`, excluidas de los paquetes y del repositorio.

La microSD contenía `DMG.rom.bin`; se creó una copia verificada como
`/GAMEBOY/DMG.ROM`, conservando el archivo original. Se comprobó:

- Tamaño: **256 bytes**.
- MD5: **32fbbd84168d3482956eb3c5051637f5**, correspondiente al boot DMG clásico.
- Ejecución desde PC=0, desplazamiento del logo Nintendo, sonido generado por
  el APU y continuación en el cartucho.
- Tetris `(W) (V1.1) [!]`: arranque original, copyright y menú GAME TYPE / MUSIC TYPE.
- Captura LCD y protocolo de grabación USB de vídeo y audio en el dispositivo.

La prueba de audio nativo registra aproximadamente **59,723 frames/s**, cola
estable de cuatro paquetes y **cero vaciados, paquetes perdidos, errores I2S y
expiraciones de espera**. La observación inicial duró 45 segundos; otra lectura
con más de 11.900 fotogramas mantuvo esos contadores a cero, tras más de tres
minutos de ejecución. Los registros completos están en `docs/hardware-20261122/`.
El binario final, con borrado automático de DMA, repitió una prueba de 60
segundos sin errores y mantuvo aproximadamente 59,7 frames/s.
El usuario confirmó que en V0.3 la música del menú se oía bien pero durante
la partida el sonido se estropeaba. La V0.2 también fue descartada por distorsión
que aparecía al rato. Los resultados anteriores de Tetris corresponden al menú,
no garantizaban la continuidad de audio en otros cartuchos durante el juego.

## Corrección V0.4: lecturas de SD durante la partida

La lectura inicial del dispositivo mostraba fotogramas de **52,4 ms**, una
cola casi vacía y un aumento de 191 a 273 vaciados de audio al reanudar la partida.
Para solo 304 fotogramas hubo 1.245 lecturas SD adicionales. El registro se
conserva en `gameplay-v03-before.txt`; es anterior al diagnóstico del nombre de
cartucho y no prueba la identidad del juego activo.

La caché de cuatro páginas de 8 KiB no podía conservar todos los bancos de un
cartucho de 64 KiB. V0.4 amplía la caché a ocho páginas (**64 KiB**), manteniendo
la paginación para cartuchos mayores. No añade esperas ni cambia el tono para
ocultar una emulación lenta. El diagnóstico USB identifica ahora el cartucho y
separa el coste de CPU/LCD del de síntesis APU.

Prueba dentro de una partida de **Mario & Yoshi**, verificada por captura LCD:
44,7 segundos medidos, **59,73 frames/s**, cola de cuatro paquetes, cero vaciados,
paquetes perdidos, errores I2S o expiraciones. Las lecturas SD permanecieron en
ocho y las faltas de caché en seis durante toda la observación. Tiempo de
fotograma de unos 11–12,5 ms. Captura y registro: `yoshi-v04.png` y
`yoshi-gameplay-v04-live.txt`.

El usuario confirmó que V0.4 seguía produciendo ruido en **Super Chase H.Q.**.
La carrera mantuvo cola de cuatro paquetes y cero vaciados, sin aumentar las
lecturas SD (`superchase-v04-active.txt` y `superchase-v04-active-after.txt`).
La continuidad del transporte no demostraba exactitud acústica del APU.

## Corrección V0.5: APU durante la carrera

Antes se ejecutaba toda la CPU del fotograma y solo entonces se sintetizaban
las 548 muestras a partir del estado final de los registros. Un efecto que
empezase y terminase dentro de ese intervalo desaparecía, y los cambios rápidos
de tono/volumen se desplazaban hasta 16,7 ms. V0.5 cuenta los ciclos transcurridos
y sintetiza las muestras pendientes **antes de cada lectura o escritura de
registros APU**, conservando el estado de los canales entre fragmentos.
La resolución es de una muestra (~30,6 µs), no se afirma exactitud ciclo a ciclo.

El promediado de transiciones dividía enteros antes de multiplicar, perdiendo
las fracciones y tomando prácticamente el último valor de onda. V0.5 acumula
el área en 64 bits y divide al final, también en los canales de onda y ruido.
Reduce aliasing; no equivale a un filtro de reconstrucción ideal.

Pruebas sintéticas nuevas con sanitizadores: pulso de 50% cuya frecuencia
integra cuatro periodos completos por muestra, continuidad entre síntesis de
un bloque y fragmentos de 17 muestras, y ROM de prueba que activa y desactiva
un sonido dentro del mismo fotograma. Todas pasan.


Prueba de V0.5 instalada: carrera activa de Super Chase H.Q. con aceleración,
48,4 segundos medidos entre diagnósticos, aproximadamente **59,73 frames/s**,
cola de cuatro paquetes y **cero vaciados, pérdidas, errores I2S y expiraciones**.
No hubo nuevas lecturas de SD durante esa observación. CPU/APU se mantuvieron
por debajo del presupuesto de audio en las lecturas de la carrera. Captura:
`superchase-v05.png`; registro: `superchase-gameplay-v05-live.txt`.
El usuario confirma la mejora auditiva de V0.5 durante la carrera: «Ahora suena
mejor». Esto no demuestra fidelidad perfecta de todos los efectos.

## Cambios de sonido

V0.3 utiliza el controlador I2S de ESP-IDF directamente con el códec ES8311 del
ADV. Evita el mezclador/remuestreador de M5Speaker para el flujo continuo.
Inicializa el códec por I2C sin arrancar y destruir el motor asíncrono de M5;
una primera prueba de esa transición reiniciaba el dispositivo y fue corregida.

- PCM mono de 32.731 muestras/s, duplicado en los dos slots I2S de 16 bits.
- El reloj I2S regula la CPU; la emulación rellena la reserva tras una pausa.
- Ceros explícitos al pausar o faltar muestras, y borrado automático de DMA.
- Mezcla ancha y 6 dB de margen antes de reducir a PCM de 16 bits.
- Volumen NR32 del canal de onda aplicado una sola vez y con nivel normalizado.
- Filtro de continua continuo entre paquetes para reducir saltos y chasquidos.
- Intérprete en RAM interna, caché rápida, escalado equivalente píxel a píxel
  y envío LCD limitado al área de imagen cuando los márgenes ya están negros.

## Pruebas automáticas

`sh tests/run_host.sh` usa C99/C++17 con AddressSanitizer y
UndefinedBehaviorSanitizer. No necesita ROMs comerciales.

- Cabeceras, checksum, tamaños malformados, cartuchos truncados y rechazo CGB exclusivo.
- Caché paginada, alternancia entre los ocho bancos de 8 KiB sin nuevas lecturas,
  lecturas fuera de rango y retirada de SD.
- Boot sintético, desmapeo FF50, reset original/directo y RAM de cartucho.
- CPU, tiles, audio, controles, opcode inválido y HALT permanente con/sin LCD.
- Interrupción VBLANK, vector, bandera HRAM, RETI y continuación del programa.
- Escalado: cada fila y ambas anchuras/paletas comparadas con el filtro de referencia.
- Cola de audio: llena/vacía, orden y 10.000 paquetes concurrentes.
- Canal de onda: normalización, niveles NR32 mitad/cuarto y margen de mezcla.
- Carpetas SD, exclusión de archivos auxiliares y partidas, orden y paginación.

`python3 tests/record_protocol.py`: cinco pruebas del empaquetado de imagen,
cadencia, muestras actuales/antiguas, datos corruptos y transporte en pseudoterminal.
La captura antigua V0.1 conserva su frecuencia original de 16.365 Hz; el grabador
admite también V0.2/V0.3 a 32.731 Hz. Los MP4 usan AAC con remuestreo de FFmpeg.

## Compilación y alcance

M5Stack ESP32 3.3.10; M5Cardputer 1.1.1, M5Unified 0.2.28 y M5GFX 0.2.35 en
copias locales verificadas. Perfil QIO de 8 MB, 240 MHz, PSRAM desactivada y USB
CDC. BIN, huella de fuentes y fecha: `firmware/BUILD-INFO.txt`, `SHA256SUMS` y
`SOURCE-SHA256SUMS`. La compilación verifica las copias C/C++ procesadas por Arduino.

La imagen `tests/.build/synthetic-gameboy.ppm` es sintética. Las capturas bajo
`docs/hardware-20261122/` proceden del Cardputer real; el vídeo USB es captura de
la emulación, no una filmación de la carcasa o del altavoz físico.

Faltan pruebas amplias de compatibilidad, partidas/cortes de alimentación,
cartuchos grandes con muchas faltas de caché, RTC persistente y sesiones de
varias horas. No se han probado todos los juegos presentes en la SD. La APU
se sincroniza con resolución de muestra y no se afirma exactitud ciclo a ciclo.
