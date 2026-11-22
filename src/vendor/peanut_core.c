#include "../core/Config.h"
#if defined(__GNUC__) && !defined(__clang__)
// The CPU interpreter is the hot path; keep the rest of the firmware at -Os.
#pragma GCC optimize ("O3")
#endif
#include <stdint.h>
#ifdef ESP_PLATFORM
#include <esp_attr.h>
#define GB_ADV_HOT IRAM_ATTR
#else
#define GB_ADV_HOT
#endif
uint8_t audio_read(uint16_t addr);
void audio_write(uint16_t addr, uint8_t value);
#include "peanut_gb.h"
