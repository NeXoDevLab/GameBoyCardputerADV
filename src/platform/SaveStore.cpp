#include "SaveStore.h"

namespace gameboy_save {
bool commit(Storage &storage,const std::string &path,const uint8_t *data,size_t bytes) {
  const auto temp=path+".tmp",backup=path+".bak";
  if(storage.exists(temp.c_str()) && !storage.remove(temp.c_str())) return false;
  if(!storage.write(temp.c_str(),data,bytes) || !storage.matches(temp.c_str(),data,bytes)) return false;
  if(storage.exists(path.c_str())) {
    if(storage.exists(backup.c_str()) && !storage.remove(backup.c_str())) return false;
    if(!storage.move(path.c_str(),backup.c_str())) return false;
  }
  return storage.move(temp.c_str(),path.c_str());
}
}
