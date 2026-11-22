#include "GameFiles.h"
#include <algorithm>
#include <utility>

namespace gameboy_files {
namespace {
bool visible(const std::string &s) {return !s.empty() && s[0]!='.' && s.find('/')==std::string::npos;}
bool inside(const std::string &s) {return s==kRoot || s.compare(0,std::string(kRoot).size()+1,std::string(kRoot)+"/")==0;}
void insert(Listing &out,Entry entry) {
  const auto before=[](const Entry &a,const Entry &b) {return a.directory!=b.directory?a.directory:a.name<b.name;};
  const auto at=std::lower_bound(out.entries.begin(),out.entries.end(),entry,before);
  if(out.entries.size()==kCapacity) {
    out.more=true;if(at==out.entries.end()) return;
    const auto index=std::size_t(at-out.entries.begin());out.entries.pop_back();
    out.entries.insert(out.entries.begin()+index,std::move(entry));
  } else out.entries.insert(at,std::move(entry));
}
bool ensure(Storage &storage,const std::string &path) {
  return storage.exists(path.c_str())?storage.directory(path.c_str()):storage.mkdir(path.c_str());
}
}
Kind kindOf(const std::string &name) {
  const auto dot=name.find_last_of('.');if(dot==std::string::npos) return Kind::none;
  auto ext=name.substr(dot+1);
  for(auto &c:ext) if(c>='A' && c<='Z') c=char(c+'a'-'A');
  return ext=="gb"?Kind::gb:Kind::none;
}
std::string baseName(const std::string &path) {
  const auto at=path.find_last_of('/');return at==std::string::npos?path:path.substr(at+1);
}
std::string child(const std::string &folder,const std::string &name) {
  if(!inside(folder) || !visible(name)) return inside(folder)?folder:kRoot;
  return folder+"/"+name;
}
std::string parent(const std::string &folder) {
  if(!inside(folder) || folder==kRoot) return kRoot;
  return folder.substr(0,folder.find_last_of('/'));
}
Listing list(Storage &storage,const std::string &folder) {
  Listing out;out.entries.reserve(kCapacity);
  if(!inside(folder)) return out;
  out.ok=storage.scan(folder.c_str(),[](void *context,const char *raw,bool directory) {
    const auto name=baseName(raw);
    if(visible(name) && (directory || kindOf(name)!=Kind::none)) insert(*static_cast<Listing*>(context),{name,directory});
  },&out);
  return out;
}
std::size_t navigate(std::size_t selected,std::size_t count,int rows,int pages) {
  if(!count) return 0;selected=std::min(selected,count-1);
  if(pages) {
    const auto page=selected/kPageRows,last=(count-1)/kPageRows;
    if((pages<0 && !page) || (pages>0 && page==last)) return selected;
    const auto next=pages>0?page+1:page-1;
    return std::min(next*kPageRows+selected%kPageRows,count-1);
  }
  if(rows<0) return (selected+count-1)%count;
  if(rows>0) return (selected+1)%count;
  return selected;
}
bool prepare(Storage &storage) {
  return ensure(storage,kSystem) && ensure(storage,kRoot) && ensure(storage,std::string(kRoot)+"/GB");
}
}
