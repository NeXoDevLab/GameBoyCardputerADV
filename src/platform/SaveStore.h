#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace gameboy_save {
struct Storage {
  virtual ~Storage()=default;
  virtual bool exists(const char *path)=0;
  virtual bool write(const char *path,const uint8_t *data,size_t bytes)=0;
  virtual bool matches(const char *path,const uint8_t *data,size_t bytes)=0;
  virtual bool remove(const char *path)=0;
  virtual bool move(const char *from,const char *to)=0;
};
// Publish only after reading back every byte. Preserve .bak across a failed
// final rename, including a retry where the main file is already missing.
bool commit(Storage&,const std::string &path,const uint8_t *data,size_t bytes);
}
