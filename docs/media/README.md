# Capturas y marco Cardputer ADV

Las cuatro imágenes de la galería y el vídeo muestran capturas USB reales de la
emulación dentro de la fotografía frontal de M5Stack utilizada en los proyectos
Amstrad CPC 464 y Spectrum 48K. Son composiciones, no fotografías de una LCD en
funcionamiento. No se recrean los gráficos de los juegos.

La plantilla se conserva en `../images/cardputer-adv-template.png` y procede de
[M5Stack Cardputer ADV](https://docs.m5stack.com/en/core/Cardputer-Adv), imagen
[Cardputer-Adv_05.webp](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1178/Cardputer-Adv_05.webp).
Se recortan sus márgenes blancos a 880×580 y se coloca la captura original a
240×135 en el rectángulo `(231, 54, 240, 135)`, igual que en los otros emuladores.
La portada de M5Burner añade márgenes blancos hasta 1280×720, con el equipo centrado.

| Composición | Captura original | Versión |
| --- | --- | --- |
| [Arranque DMG](boot-v06-cardputer-adv.png) | [LCD original](../hardware-20261122/v06-2x-boot.png) | V0.6 |
| [Super Chase H.Q.](superchase-v06-cardputer-adv.png) | [LCD original](superchase-v06.png) | V0.6 |
| [Tetris](tetris-v03-cardputer-adv.png) | [LCD original](../hardware-20261122/tetris-v03-live.png) | V0.3 |
| [Mario & Yoshi](yoshi-v04-cardputer-adv.png) | [LCD original](../hardware-20261122/yoshi-v04.png) | V0.4 |
| [Vídeo de Mario & Yoshi](gameplay-v06-cardputer-adv.mp4) | [Grabación original](gameplay-v06.mp4) | V0.6 |

El vídeo dura unos 12 segundos, conserva el movimiento y la velocidad emulada,
y copia el audio AAC original sin volver a codificarlo. La previsualización GIF
es silenciosa. El sonido USB no es una grabación acústica del altavoz.
Los hashes de las capturas, la plantilla, las composiciones y el audio se guardan
en [cardputer-gallery.json](cardputer-gallery.json).

Para regenerar las imágenes, el vídeo y la portada, con FFmpeg instalado:

```sh
python3 scripts/build-gallery-media.py
```
