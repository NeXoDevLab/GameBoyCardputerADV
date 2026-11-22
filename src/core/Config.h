#pragma once
#define ENABLE_SOUND 1
#define ENABLE_LCD 1
#define PEANUT_GB_12_COLOUR 0
#define MINIGB_APU_AUDIO_FORMAT_S16SYS 1
#define GAMEBOY_AUDIO_OVERSAMPLE 2
#define AUDIO_SAMPLE_RATE (32768 * GAMEBOY_AUDIO_OVERSAMPLE)
// Keep exactly two internal samples for each of the 548 output samples.
#define AUDIO_SAMPLES_PER_FRAME (548 * GAMEBOY_AUDIO_OVERSAMPLE)
