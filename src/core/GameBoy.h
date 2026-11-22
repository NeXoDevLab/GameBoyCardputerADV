#pragma once
#include "Cartridge.h"
#include <memory>

namespace gameboy {
constexpr unsigned kWidth=160,kHeight=144,kSamples=548,kSampleRate=32731;
constexpr uint32_t kClock=4194304,kFrameCycles=70224;
enum Button : uint8_t { A=1,B=2,Select=4,Start=8,Right=16,Left=32,Up=64,Down=128 };
class Machine {
public:
  Machine();
  ~Machine();
  Machine(const Machine&)=delete;
  Machine &operator=(const Machine&)=delete;
  bool available() const;
  bool start(Cartridge&,const uint8_t *boot,size_t bootBytes,bool original,const char *&error);
  bool reset(bool original,const char *&error);
  void stop();
  bool runFrame(bool render=true);
  void buttons(uint8_t pressed);
  const uint8_t *pixels() const;
  const int16_t *audio() const;
  uint8_t *ram();
  size_t ramSize() const;
  bool dirty() const;
  void saved();
  bool running() const;
  bool booting() const;
  uint16_t pc() const;
  uint8_t peek(uint16_t address) const;
  uint32_t cpuFlags() const;
  uint32_t frames() const;
  uint32_t coreMicros() const;
  uint32_t apuMicros() const;
  const char *error() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
void renderRow(const uint8_t *pixels,unsigned y,uint16_t *out,bool wide=false,bool grey=false);
}
