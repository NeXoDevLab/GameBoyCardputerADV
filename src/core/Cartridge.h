#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace gameboy {
struct Reader {
  virtual ~Reader() = default;
  virtual uint32_t size() const = 0;
  virtual bool readAt(uint32_t offset, uint8_t *data, size_t count) = 0;
};
struct Header {
  uint32_t romBytes=0, ramBytes=0;
  uint8_t type=0;
  bool battery=false, rtc=false;
  char title[17]{};
};
bool inspect(Reader&, Header&, const char *&error);
// Eight 8 KiB LRU pages: a 64 KiB cart fits completely; larger cartridges
// stay on the SD. No PSRAM, flash partition erasure or ROM-size allocation.
class Cartridge {
public:
  static constexpr size_t kPageSize=8192, kPages=8;
  bool mount(Reader &reader, const char *&error);
  void eject();
  bool read(uint32_t address, uint8_t &value) {
    if((address>>13)==lastIndex_) {value=lastBytes_[address&8191];return true;}
    return readPage(address,value);
  }
  const Header &header() const {return header_;}
  bool failed() const {return failed_;}
  uint32_t misses() const {return misses_;}
private:
  Reader *reader_=nullptr;
  Header header_{};
  struct Page {std::array<uint8_t,kPageSize> bytes; uint32_t index=UINT32_MAX, age=0;};
  std::array<Page,kPages> pages_{};
  uint32_t clock_=0, misses_=0;
  bool failed_=false;
  uint32_t lastIndex_=UINT32_MAX;
  const uint8_t *lastBytes_=nullptr;
  bool readPage(uint32_t address,uint8_t &value);
};
}
