#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace gameboy_settings {
constexpr const char *kPath="/GAMEBOY/SETTINGS.INI";
constexpr size_t kMaxBytes=512;
struct Settings {
  uint8_t volume=77,brightness=180;
  bool wide=false,grey=false,original=true;
  std::string lastGame;
};
// A malformed/truncated file must not partially change the live settings.
bool decode(const std::string&,Settings&);
std::string encode(const Settings&);
bool gamePath(const std::string&);
}
