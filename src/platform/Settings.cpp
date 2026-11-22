#include "Settings.h"
#include "GameFiles.h"

namespace gameboy_settings {
bool gamePath(const std::string &path) {
  if(path.empty()) return true;
  if(path.size()>240 || path.compare(0,15,"/GAMEBOY/GAMES/")!=0 || gameboy_files::kindOf(path)!=gameboy_files::Kind::gb) return false;
  size_t at=1;
  while(at<path.size()) {
    auto end=path.find('/',at);if(end==std::string::npos) end=path.size();
    const auto part=path.substr(at,end-at);
    if(part.empty() || part=="." || part=="..") return false;
    for(unsigned char c:part) if(c<32 || c==127) return false;
    at=end+1;
  }
  return true;
}
bool decode(const std::string &text,Settings &out) {
  if(text.size()>kMaxBytes || text.compare(0,7,"GBADV1\n")!=0 || text.back()!='\n') return false;
  Settings result;unsigned seen=0;size_t at=7;
  while(at<text.size()) {
    const auto end=text.find('\n',at);if(end==std::string::npos) return false;
    const auto line=text.substr(at,end-at);at=end+1;
    const auto equals=line.find('=');if(equals==std::string::npos) return false;
    const auto key=line.substr(0,equals),value=line.substr(equals+1);
    if(key=="last") {
      if((seen&32) || !gamePath(value)) return false;
      seen|=32;result.lastGame=value;continue;
    }
    const unsigned mask=key=="volume"?1:key=="brightness"?2:key=="wide"?4:key=="grey"?8:key=="original"?16:0;
    if(!mask) continue;
    if((seen&mask) || value.empty() || value.size()>3) return false;
    unsigned number=0;for(char c:value) {if(c<'0' || c>'9') return false;number=number*10+unsigned(c-'0');}
    if(number>255 || (mask>=4 && number>1) || (mask==2 && number<32)) return false;
    seen|=mask;
    switch(mask) {
      case 1:result.volume=uint8_t(number);break;
      case 2:result.brightness=uint8_t(number);break;
      case 4:result.wide=number;break;
      case 8:result.grey=number;break;
      case 16:result.original=number;break;
    }
  }
  if(seen!=63) return false;
  out=result;return true;
}
std::string encode(const Settings &s) {
  return "GBADV1\nvolume="+std::to_string(s.volume)+"\nbrightness="+std::to_string(s.brightness)+
    "\nwide="+std::to_string(s.wide)+"\ngrey="+std::to_string(s.grey)+"\noriginal="+
    std::to_string(s.original)+"\nlast="+(gamePath(s.lastGame)?s.lastGame:"")+"\n";
}
}
