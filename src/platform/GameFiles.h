#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace gameboy_files {
constexpr const char *kSystem="/GAMEBOY";
constexpr const char *kRoot="/GAMEBOY/GAMES";
constexpr std::size_t kCapacity=128,kPageRows=6;
enum class Kind { none,gb };
struct Entry {std::string name;bool directory=false;};
struct Listing {std::vector<Entry> entries;bool ok=false,more=false;};
using Visitor=void(*)(void*,const char*,bool);
struct Storage {
  virtual ~Storage()=default;
  virtual bool scan(const char*,Visitor,void*)=0;
  virtual bool exists(const char*)=0;
  virtual bool directory(const char*)=0;
  virtual bool mkdir(const char*)=0;
  virtual bool move(const char*,const char*)=0;
};
Kind kindOf(const std::string&);
std::string baseName(const std::string&);
std::string child(const std::string&,const std::string&);
std::string parent(const std::string&);
Listing list(Storage&,const std::string&);
std::size_t navigate(std::size_t selected,std::size_t count,int rows,int pages);
bool prepare(Storage&);
}
