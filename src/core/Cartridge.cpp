#include "Cartridge.h"
#include <algorithm>
#include <cstring>

namespace gameboy {
bool inspect(Reader &reader, Header &out, const char *&error) {
  error=nullptr; uint8_t bytes[0x150];
  if(reader.size()<32768 || !reader.readAt(0,bytes,sizeof(bytes))) {error="Cartucho truncado o ilegible";return false;}
  if(bytes[0x143]==0xc0) {error="Solo Game Boy Color: no compatible";return false;}
  if(bytes[0x148]>8 || bytes[0x149]>5) {error="Tamano de ROM/RAM no compatible";return false;}
  Header h;h.type=bytes[0x147];h.romBytes=32768u<<bytes[0x148];
  if(reader.size()!=h.romBytes) {error="Tamano distinto a la cabecera GB";return false;}
  uint8_t checksum=0;
  for(unsigned i=0x134;i<=0x14c;++i) checksum=uint8_t(checksum-bytes[i]-1);
  if(checksum!=bytes[0x14d]) {error="Checksum de cabecera incorrecto";return false;}
  bool ram=false;
  switch(h.type) {
    case 0x00:case 0x01:case 0x11:case 0x19:break;
    case 0x02:case 0x08:case 0x12:case 0x1a:ram=true;break;
    case 0x03:case 0x09:case 0x13:case 0x1b:ram=true;h.battery=true;break;
    case 0x05:case 0x06:h.ramBytes=512;h.battery=h.type==6;break;
    case 0x0f:h.battery=h.rtc=true;break;
    case 0x10:ram=h.battery=h.rtc=true;break;
    default:error="Controlador de cartucho no compatible";return false;
  }
  constexpr uint32_t ramSizes[]={0,2048,8192,32768,131072,65536};
  if(ram) h.ramBytes=ramSizes[bytes[0x149]];
  const unsigned titleEnd=(bytes[0x143]&0x80)?0x13f:0x144;
  for(unsigned i=0x134;i<titleEnd && bytes[i];++i)
    h.title[i-0x134]=(bytes[i]>=32 && bytes[i]<127)?char(bytes[i]):'?';
  out=h;return true;
}
bool Cartridge::mount(Reader &reader, const char *&error) {
  Header h;if(!inspect(reader,h,error)) return false;
  eject();reader_=&reader;header_=h;return true;
}
void Cartridge::eject() {
  reader_=nullptr;header_={};clock_=misses_=0;failed_=false;
  lastIndex_=UINT32_MAX;lastBytes_=nullptr;
  for(auto &page:pages_) {page.index=UINT32_MAX;page.age=0;}
}
bool Cartridge::readPage(uint32_t address,uint8_t &value) {
  value=0xff;
  if(!reader_) return false;
  if(address>=header_.romBytes) return true;
  const auto index=address/kPageSize;
  Page *slot=nullptr;
  for(auto &p:pages_) if(p.index==index) {slot=&p;break;}
  if(!slot) {
    slot=&*std::min_element(pages_.begin(),pages_.end(),[](const Page &a,const Page &b){return a.age<b.age;});
    slot->index=UINT32_MAX;++misses_;
    if(!reader_->readAt(index*kPageSize,slot->bytes.data(),kPageSize)) {failed_=true;return false;}
    slot->index=index;
  }
  // Rebase before wrapping so an uninterrupted long session keeps its LRU order.
  if(clock_==UINT32_MAX) {
    std::array<size_t,kPages> order{};
    for(size_t i=0;i<kPages;++i) order[i]=i;
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return pages_[a].age<pages_[b].age;});
    clock_=0;for(auto i:order) if(pages_[i].index!=UINT32_MAX) pages_[i].age=++clock_;
  }
  slot->age=++clock_;lastIndex_=index;lastBytes_=slot->bytes.data();value=lastBytes_[address&8191];return true;
}
}
