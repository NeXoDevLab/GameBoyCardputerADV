#pragma once
#include "../core/GameBoy.h"
#include <array>
#include <atomic>
#include <cstring>
namespace gameboy_audio {
// One producer (CPU), one consumer (speaker worker). Clear only while stopped.
class Queue {
public:
  static constexpr unsigned kPackets=16;
  bool push(const int16_t *samples) {
    const auto w=write_.load(std::memory_order_relaxed);
    if(w-read_.load(std::memory_order_acquire)==kPackets) {++dropped_;return false;}
    std::memcpy(data_[w%kPackets].data(),samples,gameboy::kSamples*sizeof(int16_t));
    write_.store(w+1,std::memory_order_release);return true;
  }
  bool pop(int16_t *out) {
    const auto r=read_.load(std::memory_order_relaxed);
    if(r==write_.load(std::memory_order_acquire)) return false;
    std::memcpy(out,data_[r%kPackets].data(),gameboy::kSamples*sizeof(int16_t));
    read_.store(r+1,std::memory_order_release);return true;
  }
  uint32_t packets() const {const auto r=read_.load(std::memory_order_acquire);return write_.load(std::memory_order_acquire)-r;}
  uint32_t dropped() const {return dropped_.load();}
  void clear() {read_=0;write_=0;}
private:
  std::array<std::array<int16_t,gameboy::kSamples>,kPackets> data_{};
  std::atomic<uint32_t> read_{0},write_{0},dropped_{0};
};
}
