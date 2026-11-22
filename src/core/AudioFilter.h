#pragma once
#include <algorithm>
#include <array>
#include <cstdint>

namespace gameboy_audio {
// 31-tap Hamming-windowed sinc, cutoff 0.20 of the internal 65.5 kHz rate.
// Q15 coefficients sum to unity. Push both internal samples, read after each
// pair. State persists across frames; total group delay is ~0.23 ms.
class LowPass2x {
public:
  void reset() {history_.fill(0);cursor_=0;}
  void push(int16_t value) {history_[cursor_]=value;if(++cursor_==history_.size()) cursor_=0;}
  int16_t sample() const {
    static constexpr int16_t coefficients[]={0,-64,-57,86,210,0,-439,-378,516,1130,0,
      -2107,-1868,2949,9839,13134,9839,2949,-1868,-2107,0,1130,516,-378,-439,0,210,86,-57,-64,0};
    // sum(abs(coefficients))=52420, so every int16 input fits in int32.
    int32_t total=0;size_t index=cursor_;
    for(auto weight:coefficients) {
      if(!index) index=history_.size();--index;
      total+=int32_t(history_[index])*weight;
    }
    return int16_t(std::max<int32_t>(-32768,std::min<int32_t>(32767,total/32768)));
  }
private:
  std::array<int16_t,31> history_{};
  size_t cursor_=0;
};
}
