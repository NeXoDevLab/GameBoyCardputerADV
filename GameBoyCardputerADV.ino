#include "src/platform/CardputerPlatform.h"
void setup() { gameboy_platform::begin(); }
void loop() { gameboy_platform::tick(); }
