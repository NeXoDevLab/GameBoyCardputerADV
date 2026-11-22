#include "../src/core/GameBoy.h"
#include "../src/platform/GameFiles.h"
#include "../src/platform/AudioQueue.h"
#include "../src/platform/SaveStore.h"
#include "../src/platform/Settings.h"
#include "../src/core/AudioFilter.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <random>
#include <map>
#include <cmath>
#include "../src/core/Config.h"
extern "C" {
#define PEANUT_GB_HEADER_ONLY
#include "../src/vendor/peanut_gb.h"
#include "../src/vendor/minigb_apu.h"
uint8_t __gb_read(struct gb_s*,uint16_t);
void __gb_write(struct gb_s*,uint_fast16_t,uint8_t);
}
using namespace gameboy;
struct MemoryReader final : Reader {
  std::vector<uint8_t> data;
  bool fail=false;unsigned reads=0;
  explicit MemoryReader(std::vector<uint8_t> value):data(std::move(value)) {}
  uint32_t size() const override {return uint32_t(data.size());}
  bool readAt(uint32_t at,uint8_t *out,size_t n) override {
    ++reads;if(fail || at>data.size() || n>data.size()-at) return false;
    std::memcpy(out,data.data()+at,n);return true;
  }
};
void checksum(std::vector<uint8_t> &rom) {
  uint8_t sum=0;for(unsigned i=0x134;i<=0x14c;++i) sum=uint8_t(sum-rom[i]-1);rom[0x14d]=sum;
}
std::vector<uint8_t> makeRom(uint8_t type=0,uint8_t size=0,uint8_t ram=0) {
  std::vector<uint8_t> rom(32768u<<size,0);
  std::memcpy(rom.data()+0x134,"NEXO GB TEST",12);
  rom[0x147]=type;rom[0x148]=size;rom[0x149]=ram;
  // Entry jumps over the cartridge header into original synthetic code.
  rom[0x100]=0xc3;rom[0x101]=0x50;rom[0x102]=0x01;
  checksum(rom);return rom;
}
void cartridgeChecks() {
  const char *error=nullptr;Header h;
  MemoryReader input(makeRom());assert(inspect(input,h,error));assert(h.romBytes==32768 && h.ramBytes==0);
  Cartridge c;assert(c.mount(input,error));uint8_t byte=0;
  for(uint32_t i=0;i<input.size();++i) {assert(c.read(i,byte));assert(byte==input.data[i]);}
  const auto reads=input.reads;
  for(uint32_t i=0;i<input.size();++i) assert(c.read(i,byte));
  assert(input.reads==reads && c.misses()==4);
  // Gameplay can jump between all four 16 KiB banks each frame. Once warm,
  // a 64 KiB cart must never block audio with repeated SD page reads.
  input.data=makeRom(1,1);assert(c.mount(input,error));
  for(unsigned pass=0;pass<100;++pass)
    for(unsigned page: {0u,2u,4u,6u,1u,3u,5u,7u}) {
      assert(c.read(page*Cartridge::kPageSize,byte));
      assert(byte==input.data[page*Cartridge::kPageSize]);
    }
  assert(c.misses()==8);
  for(unsigned code:{9,255}) {input.data[0x148]=uint8_t(code);checksum(input.data);assert(!inspect(input,h,error));}
  input.data=makeRom();input.data[0x149]=255;checksum(input.data);assert(!inspect(input,h,error));
  input.data=makeRom();input.data[0x143]=0xc0;checksum(input.data);assert(!inspect(input,h,error));
  input.data=makeRom();input.data.pop_back();assert(!inspect(input,h,error));
  input.data=makeRom();input.data[0x134]^=1;assert(!inspect(input,h,error));
  input.data=makeRom(0x22);assert(!inspect(input,h,error));
  input.data=makeRom(6);assert(inspect(input,h,error) && h.ramBytes==512 && h.battery);
  input.data=makeRom(0x10,6,3);assert(inspect(input,h,error) && h.romBytes==2097152 && h.ramBytes==32768 && h.rtc);
  assert(c.mount(input,error));
  for(unsigned page=0;page<100;++page) {const uint32_t at=(page*31337u)%input.size();assert(c.read(at,byte));assert(byte==input.data[at]);}
  input.fail=true;assert(!c.read(input.size()-1,byte) && c.failed());
  puts("OK cartridge headers, malformed sizes, CGB-only rejection and banked SD cache");
}
std::vector<uint8_t> testProgram() {
  auto rom=makeRom(3,0,3);
  const uint8_t code[]={
    0xf3,0x31,0xfe,0xff, // DI; SP
    0xfa,0x00,0x40, // also exercise a switchable ROM page
    0xaf,0xe0,0x40, // LCD off
    0x3e,0x0a,0xea,0x00,0x00, // enable battery RAM
    0x3e,0x5a,0xea,0x00,0xa0, // RAM marker
    0xfa,0x00,0xc0,0xea,0x02,0xa0, // copy marker written by test boot
    0x3e,0xe4,0xe0,0x47, // identity palette
    0x21,0x00,0x80,0x06,0x10,0x3e,0xaa, // patterned tile
    0x22,0x2f,0x05,0x20,0xfb,
    0x3e,0x80,0xe0,0x26, // sound power
    0x3e,0x77,0xe0,0x24,0x3e,0x11,0xe0,0x25,
    0x3e,0x80,0xe0,0x11,0x3e,0xf3,0xe0,0x12,
    0x3e,0x83,0xe0,0x13,0x3e,0x87,0xe0,0x14,
    0x3e,0x91,0xe0,0x40, // LCD on
    0x3e,0x10,0xe0,0x00, // buttons group A/B/Select/Start
    0xf0,0x00,0xea,0x01,0xa0,0x18,0xf5 // select and poll JOYP into cart RAM
  };
  std::copy(std::begin(code),std::end(code),rom.begin()+0x150);return rom;
}
void bootAndCore(const std::filesystem::path &out) {
  MemoryReader reader(testProgram());Cartridge cart;const char *error=nullptr;
  assert(cart.mount(reader,error));Machine m;assert(m.available());
  assert(!m.start(cart,nullptr,0,true,error));assert(error && !m.running());
  // Original test boot code proves mapping at PC=0, real CPU execution and
  // FF50 unmapping at 0x100. It contains no Nintendo bytes or artwork.
  uint8_t boot[256]{};
  const uint8_t begin[]={0x31,0xfe,0xff,0x3e,0x42,0xea,0x00,0xc0,0xc3,0xfc,0x00};
  std::copy(std::begin(begin),std::end(begin),boot);
  boot[0xfc]=0x3e;boot[0xfd]=1;boot[0xfe]=0xe0;boot[0xff]=0x50;
  assert(m.start(cart,boot,256,true,error));assert(m.pc()==0 && m.booting());
  m.buttons(A|Start);
  bool audible=false,pattern=false;
  for(unsigned i=0;i<180;++i) {
    assert(m.runFrame());
    for(unsigned s=0;s<kSamples;++s) audible|=m.audio()[s]!=0;
    for(unsigned p=1;p<kWidth*kHeight;++p) pattern|=m.pixels()[p]!=m.pixels()[0];
  }
  assert(!m.booting() && audible && pattern && m.dirty());
  assert(m.ramSize()==32768 && m.ram()[0]==0x5a && m.ram()[2]==0x42 && (m.ram()[1]&0xf)==6);
  m.saved();assert(!m.dirty());m.buttons(0);assert(m.runFrame());assert((m.ram()[1]&0xf)==15 && m.dirty());
  m.ram()[10]=0x73;assert(m.reset(true,error));assert(m.pc()==0 && m.booting() && m.ram()[10]==0x73);
  assert(m.reset(false,error));assert(m.pc()==0x100 && !m.booting());
  for(unsigned i=0;i<3;++i) assert(m.runFrame());
  std::ofstream ppm(out/"synthetic-gameboy.ppm",std::ios::binary);ppm<<"P6\n240 135\n255\n";
  for(unsigned y=0;y<135;++y) {uint16_t line[240];renderRow(m.pixels(),y,line);
    for(uint16_t c:line) {const char rgb[]={char(((c>>11)&31)*255/31),char(((c>>5)&63)*255/63),char((c&31)*255/31)};ppm.write(rgb,3);}}
  puts("OK boot ROM mapping/unmapping, reset, CPU, LCD, four-channel APU, controls and battery RAM");
  auto bad=makeRom();bad[0x150]=0xd3;MemoryReader invalid(bad);assert(cart.mount(invalid,error));
  assert(m.start(cart,nullptr,0,false,error));assert(!m.runFrame() && !m.running() && m.error());
  puts("OK invalid opcode exits safely without returning into upstream unreachable code");
  reader.data=testProgram();reader.fail=false;assert(cart.mount(reader,error));
  assert(m.start(cart,nullptr,0,false,error));reader.fail=true;
  assert(!m.runFrame() && !m.running());
  puts("OK SD removal stops CPU and reports a read error");
  for(bool lcd:{false,true}) {
    auto halted=makeRom();
    const uint8_t haltCode[]={0xf3,0xaf,0xe0,0xff,0xe0,0x0f,0x3e,uint8_t(lcd?0x91:0),0xe0,0x40,0x76,0xd3};
    std::copy(std::begin(haltCode),std::end(haltCode),halted.begin()+0x150);
    MemoryReader haltReader(halted);assert(cart.mount(haltReader,error));
    assert(m.start(cart,nullptr,0,false,error));
    for(unsigned frame=0;frame<10;++frame) {assert(m.runFrame());assert(m.pc()==0x15b);}
    m.stop();
  }
  puts("OK permanent HALT preserves PC and yields frames with LCD on/off and no interrupts");
}
void scaling() {
  std::vector<uint8_t> p(kWidth*kHeight,3);uint16_t row[240];
  renderRow(p.data(),0,row);assert(row[0]==0 && row[44]==0 && row[45]==0xc6d5 && row[194]==0xc6d5 && row[195]==0);
  renderRow(p.data(),134,row,true,true);for(auto c:row) assert(c==0xffff);
  p.assign(p.size(),0);renderRow(p.data(),0,row,false,true);for(auto c:row) assert(c==0);
  // A one-pixel stroke must contribute after reduction, including last row.
  p.assign(p.size(),3);for(unsigned y=0;y<144;++y) p[y*160+159]=0;
  renderRow(p.data(),134,row,false,true);assert(row[194]!=0xffff && row[193]==0xffff);
  // Compare the optimized 16:15/3:2 paths against the original area filter
  // over every output row and random source pixels, in both modes/palettes.
  std::mt19937 rng(20261122);
  for(auto &pixel:p) pixel=uint8_t(rng()&3);
  for(bool wide:{false,true}) for(bool grey:{false,true}) for(unsigned y=0;y<135;++y) {
    renderRow(p.data(),y,row,wide,grey);
    const uint16_t green[]={0x09c4,0x32a5,0x8c91,0xc6d5},grayscale[]={0,0x52aa,0xad55,0xffff};
    const auto *pal=grey?grayscale:green;
    const unsigned width=wide?240:150,offset=(240-width)/2,y0=y*144,y1=(y+1)*144;
    for(unsigned x=0;x<width;++x) {
      const unsigned x0=x*160,x1=(x+1)*160;unsigned r=0,g=0,b=0;
      for(unsigned sy=y0/135;sy<=(y1-1)/135;++sy) {
        const unsigned wy=std::min(y1,(sy+1)*135)-std::max(y0,sy*135);
        for(unsigned sx=x0/width;sx<=(x1-1)/width;++sx) {
          const unsigned weight=wy*(std::min(x1,(sx+1)*width)-std::max(x0,sx*width));
          const auto c=pal[p[sy*160+sx]];r+=((c>>11)&31)*weight;g+=((c>>5)&63)*weight;b+=(c&31)*weight;
        }
      }
      constexpr unsigned area=160*144;
      assert(row[offset+x]==uint16_t(((r+area/2)/area)<<11|((g+area/2)/area)<<5|((b+area/2)/area)));
    }
  }
  puts("OK proportional 150x135, wide 240x135, green/grey and filtered thin strokes");
  puts("OK optimized scaler matches reference pixel-for-pixel on all rows, palettes and widths");
}
void joypad() {
  for(unsigned select:{0u,16u,32u,48u}) for(unsigned pressed=0;pressed<256;++pressed) {
    gb_s gb{};gb.direct.joypad=0xff;gb.hram_io[0]=0xff;
    __gb_write(&gb,0xff00,uint8_t(select|15));
    gb_set_joypad(&gb,uint8_t(~pressed));
    uint8_t nibble=15;
    if(!(select&16)) nibble&=uint8_t(~pressed)>>4;
    if(!(select&32)) nibble&=uint8_t(~pressed)&15;
    assert(__gb_read(&gb,0xff00)==(0xc0|select|nibble));
    assert(bool(gb.hram_io[0x0f]&16)==(nibble!=15));
    gb.hram_io[0x0f]=0;
    gb_set_joypad(&gb,uint8_t(~pressed));assert(!(gb.hram_io[0x0f]&16));
    gb_set_joypad(&gb,0xff);assert(!(gb.hram_io[0x0f]&16));
  }
  gb_s gb{};gb.direct.joypad=0xfe;gb.hram_io[0]=0xff;
  __gb_write(&gb,0xff00,0x1f);assert(gb.hram_io[0x0f]&16);
  auto rom=makeRom();
  const uint8_t handler[]={0x21,1,0xc0,0x34,0xd9};
  std::copy(std::begin(handler),std::end(handler),rom.begin()+0x60);
  const uint8_t code[]={0xf3,0x31,0xfe,0xff,0xaf,0xe0,0x40,0xe0,0x0f,
    0x3e,0x10,0xe0,0xff,0xe0,0x00,0xfb,0x00,0x76,0x18,0xfd};
  std::copy(std::begin(code),std::end(code),rom.begin()+0x150);
  MemoryReader reader(rom);Cartridge cart;const char *error=nullptr;assert(cart.mount(reader,error));
  Machine m;assert(m.start(cart,nullptr,0,false,error));assert(m.runFrame());
  assert(m.peek(0xc001)==0);m.buttons(Start);assert(m.runFrame());assert(m.peek(0xc001)==1);
  for(unsigned i=0;i<4;++i) {m.buttons(Start);assert(m.runFrame());assert(m.peek(0xc001)==1);}
  m.buttons(0);m.buttons(Start);assert(m.runFrame());assert(m.peek(0xc001)==2);
  puts("OK JOYP matrix, read-only bits, selected input edges and HALT wake via joypad interrupt");
}
void alternateRendering() {
  MemoryReader reader(testProgram());Cartridge cart;const char *error=nullptr;assert(cart.mount(reader,error));
  Machine full,reduced;assert(full.start(cart,nullptr,0,false,error));assert(reduced.start(cart,nullptr,0,false,error));
  for(unsigned frame=0;frame<40;++frame) {
    const bool render=frame&1;full.buttons(uint8_t(frame));reduced.buttons(uint8_t(frame));
    assert(full.runFrame());assert(reduced.runFrame(render));
    assert(full.pc()==reduced.pc() && full.cpuFlags()==reduced.cpuFlags());
    assert(std::equal(full.audio(),full.audio()+kSamples,reduced.audio()));
    assert(std::equal(full.ram(),full.ram()+full.ramSize(),reduced.ram()));
    if(render) assert(std::equal(full.pixels(),full.pixels()+kWidth*kHeight,reduced.pixels()));
  }
  puts("OK alternate pixel rendering preserves CPU, RAM, audio and every displayed pixel");
}
void persistence() {
  using namespace gameboy_settings;
  Settings settings,loaded;assert(decode(encode(settings),loaded));
  settings.volume=120;settings.brightness=64;settings.grey=settings.wide=true;settings.original=false;
  settings.lastGame="/GAMEBOY/GAMES/GB/Mario & Yoshi (E) [!].gb";
  const auto encoded=encode(settings);assert(decode(encoded,loaded));assert(encode(loaded)==encoded);
  for(const auto &bad: {std::string{},encoded.substr(0,encoded.size()-1),
      std::string("GBADV1\nvolume=255\n"),encoded+"volume=4\n",encoded+"grey=4\n",std::string(513,'x')}) {
    assert(!decode(bad,loaded));assert(encode(loaded)==encoded);
  }
  assert(!gamePath("/GAMEBOY/GAMES/../OTHER/x.gb"));assert(!gamePath("/GAMEBOY/GAMES//x.gb"));
  assert(!gamePath("/GAMEBOY/GAMES/x.gb\n"));assert(!gamePath("/OTHER/x.gb"));
  struct MemoryStore final : gameboy_save::Storage {
    std::map<std::string,std::vector<uint8_t>> files;
    bool corrupt=false,failWrite=false,failRemove=false;unsigned failMove=0,moves=0;
    bool exists(const char *p) override {return files.count(p);}
    bool write(const char *p,const uint8_t *data,size_t bytes) override {
      files[p]={data,data+bytes};if(corrupt && bytes) files[p][bytes-1]^=1;return !failWrite;
    }
    bool matches(const char *p,const uint8_t *data,size_t bytes) override {
      return exists(p) && files[p].size()==bytes && std::equal(files[p].begin(),files[p].end(),data);
    }
    bool remove(const char *p) override {if(failRemove) return false;return files.erase(p);}
    bool move(const char *a,const char *b) override {
      if(++moves==failMove || !exists(a) || exists(b)) return false;
      files[b]=files[a];files.erase(a);return true;
    }
  } store;
  const uint8_t old[]={1,2,3},next[]={4,5,6};
  assert(gameboy_save::commit(store,"x",old,3));assert(store.files["x"]==std::vector<uint8_t>({1,2,3}));
  for(unsigned failure=0;failure<5;++failure) {
    auto trial=store;trial.corrupt=failure==0;trial.failWrite=failure==1;trial.failRemove=failure==2;
    trial.files["x.tmp"]={9};trial.moves=0;trial.failMove=failure>=3?failure-2:0;
    assert(!gameboy_save::commit(trial,"x",next,3));
    const auto survivor=trial.exists("x")?"x":"x.bak";
    assert(trial.files[survivor]==std::vector<uint8_t>({1,2,3}));
    trial.corrupt=trial.failWrite=trial.failRemove=false;trial.failMove=0;
    assert(gameboy_save::commit(trial,"x",next,3));
    assert(trial.files["x"]==std::vector<uint8_t>({4,5,6}));
    assert(trial.files["x.bak"]==std::vector<uint8_t>({1,2,3}));
  }
  puts("OK settings validation and verified save recovery after corruption, write/remove/rename failures");
}
void audioQueue() {
  gameboy_audio::Queue q;int16_t out[kSamples];
  assert(!q.pop(out));
  for(unsigned n=0;n<q.kPackets;++n) {std::fill(out,out+kSamples,int16_t(n));assert(q.push(out));}
  assert(!q.push(out) && q.dropped()==1);
  for(unsigned n=0;n<q.kPackets;++n) {assert(q.pop(out));for(auto v:out) assert(v==int16_t(n));}
  assert(!q.pop(out));q.clear();
  constexpr unsigned packets=10000;
  std::thread consumer([&]{int16_t data[kSamples];for(unsigned n=0;n<packets;++n) {
    while(!q.pop(data)) std::this_thread::yield();for(auto v:data) assert(v==int16_t(n));}});
  for(unsigned n=0;n<packets;++n) {std::fill(out,out+kSamples,int16_t(n));while(!q.push(out)) std::this_thread::yield();}
  consumer.join();assert(q.packets()==0);
  puts("OK audio ring: full/empty, ordering, wraparound and concurrent producer/consumer");
}
void interrupts() {
  auto rom=makeRom(3,0,2);
  const uint8_t isr[]={0xf5,0xf0,0x85,0x3c,0xe0,0x85,0xf1,0xd9};
  std::copy(std::begin(isr),std::end(isr),rom.begin()+0x40);
  const uint8_t program[]={0xf3,0xaf,0xe0,0x40,0xe0,0x85,0x31,0xfe,0xff,
    0x3e,0x0a,0xea,0,0,0x3e,1,0xea,0xff,0xff,0xfb,0x3e,0x91,0xe0,0x40,
    0xf0,0x85,0xea,0,0xa0,0x18,0xf9};
  std::copy(std::begin(program),std::end(program),rom.begin()+0x150);
  MemoryReader input(rom);Cartridge c;const char *error=nullptr;assert(c.mount(input,error));
  Machine m;assert(m.start(c,nullptr,0,false,error));
  for(unsigned i=0;i<20;++i) assert(m.runFrame());
  assert(m.ram()[0]>10 && m.ram()[0]<30);
  puts("OK VBLANK interrupt vector, HRAM flag, RETI and resumed main loop");
}
void apuLevels() {
  auto level=[](unsigned volume,uint8_t wave) {
    minigb_apu_ctx apu{};minigb_apu_audio_init(&apu);
    minigb_apu_audio_write(&apu,0xff26,0x80);
    minigb_apu_audio_write(&apu,0xff24,0x77);
    minigb_apu_audio_write(&apu,0xff25,0x44);
    minigb_apu_audio_write(&apu,0xff1a,0x80);
    minigb_apu_audio_write(&apu,0xff1c,uint8_t(volume<<5));
    for(unsigned a=0xff30;a<=0xff3f;++a) minigb_apu_audio_write(&apu,a,wave);
    minigb_apu_audio_write(&apu,0xff1d,0x40);minigb_apu_audio_write(&apu,0xff1e,0x83);
    int16_t samples[AUDIO_SAMPLES_TOTAL];minigb_apu_audio_callback(&apu,samples);
    int peak=0;for(auto v:samples) peak=std::max(peak,std::abs(int(v)));
    return peak;
  };
  const int full=level(1,0),half=level(2,0),quarter=level(3,0);
  assert(full>3000 && full<5000);
  assert(std::abs(full-half*2)<16 && std::abs(full-quarter*4)<32);
  assert(level(1,0xff)>2000 && level(1,0xff)<5000);
  puts("OK wave DAC level, true half/quarter NR32 volume and wide mixing headroom");
}
void apuTiming() {
  minigb_apu_ctx apu{};minigb_apu_audio_init(&apu);
  minigb_apu_audio_write(&apu,0xff26,0);minigb_apu_audio_write(&apu,0xff26,0x80);
  minigb_apu_audio_write(&apu,0xff24,0x77);minigb_apu_audio_write(&apu,0xff25,0x22);
  minigb_apu_audio_write(&apu,0xff16,0x80);minigb_apu_audio_write(&apu,0xff17,0xf0);
  minigb_apu_audio_write(&apu,0xff18,0xff);minigb_apu_audio_write(&apu,0xff19,0x87);
  int16_t full[AUDIO_SAMPLES_TOTAL]{},parts[AUDIO_SAMPLES_TOTAL]{};
  auto split=apu;minigb_apu_audio_callback(&apu,full);
  // Complete periods of a 50% pulse fit in each internal sample. Their mean is
  // zero; sampling only the final transition aliases this to a loud DC tone.
  for(unsigned i=2;i<AUDIO_SAMPLES_TOTAL;++i) assert(std::abs(int(full[i]))<12);
  unsigned done=0;while(done<AUDIO_SAMPLES) {
    unsigned count=std::min(17u,AUDIO_SAMPLES-done);
    minigb_apu_audio_render(&split,parts+2*done,count);done+=count;
  }
  assert(std::equal(std::begin(full),std::end(full),std::begin(parts)));
  auto rom=makeRom();
  const uint8_t code[]={0xf3,0xaf,0xe0,0x40,0xe0,0x26,0x3e,0x80,0xe0,0x26,
    0x3e,0x77,0xe0,0x24,0x3e,0x22,0xe0,0x25,0x3e,0x80,0xe0,0x16,
    0x3e,0xf0,0xe0,0x17,0xaf,0xe0,0x18,0x3e,0x86,0xe0,0x19,
    0x0e,4,0x06,255,0x05,0x20,0xfd,0x0d,0x20,0xf8,
    0xaf,0xe0,0x17,0x18,0xfe};
  std::copy(std::begin(code),std::end(code),rom.begin()+0x150);
  MemoryReader reader(rom);Cartridge cart;const char *error=nullptr;assert(cart.mount(reader,error));
  Machine m;assert(m.start(cart,nullptr,0,false,error));assert(m.runFrame());
  int early=0,late=0;
  for(unsigned i=0;i<120;++i) early=std::max(early,std::abs(int(m.audio()[i])));
  for(unsigned i=kSamples-100;i<kSamples;++i) late=std::max(late,std::abs(int(m.audio()[i])));
  assert(early>1000 && late<early/4);
  puts("OK pulse anti-alias integration, split-render continuity and sub-frame sound register timing");
}
void audioFilter() {
  auto rms=[](double frequency) {
    gameboy_audio::LowPass2x filter;double energy=0;unsigned count=0;
    for(unsigned i=0;i<8192;++i) {
      filter.push(int16_t(std::lround(8000*std::sin(2*3.141592653589793*frequency*i/AUDIO_SAMPLE_RATE))));
      if((i&1) && i>128) {const double sample=filter.sample();energy+=sample*sample;++count;}
    }
    return std::sqrt(energy/count);
  };
  assert(rms(1000)>5500 && rms(1000)<5800);
  assert(rms(20000)<25 && rms(22000)<25 && rms(25000)<25);
  gameboy_audio::LowPass2x filter;
  for(unsigned i=0;i<40;++i) filter.push(1000);
  assert(filter.sample()==1000);filter.reset();assert(filter.sample()==0);
  puts("OK 2x FIR: passband level, >47 dB rejection of aliasing tones, DC unity and reset");
}
namespace fs=std::filesystem;
struct HostStorage final : gameboy_files::Storage {
  fs::path root;
  explicit HostStorage(fs::path r):root(std::move(r)) {}
  fs::path path(const char *p) {return root/fs::path(p).relative_path();}
  bool scan(const char *p,gameboy_files::Visitor fn,void *ctx) override {
    if(!fs::is_directory(path(p))) return false;
    for(const auto &f:fs::directory_iterator(path(p))) fn(ctx,f.path().filename().string().c_str(),f.is_directory());return true;
  }
  bool exists(const char *p) override {return fs::exists(path(p));}
  bool directory(const char *p) override {return fs::is_directory(path(p));}
  bool mkdir(const char *p) override {return fs::create_directory(path(p));}
  bool move(const char*,const char*) override {assert(false);return false;}
};
void files(const fs::path &out) {
  using namespace gameboy_files;HostStorage s(out/"sd");fs::remove_all(s.root);fs::create_directories(s.root);
  assert(prepare(s) && prepare(s));assert(s.directory("/GAMEBOY/GAMES/GB"));
  auto put=[&](const std::string &p){std::ofstream(s.path(p.c_str()))<<"preserve";};
  put("/GAMEBOY/DMG.ROM");put(std::string(kRoot)+"/root.GB");
  put(std::string(kRoot)+"/GB/game.gb");put(std::string(kRoot)+"/GB/game.sav");
  put(std::string(kRoot)+"/GB/._hidden.gb");put(std::string(kRoot)+"/GB/color.gbc");
  auto list=gameboy_files::list(s,std::string(kRoot)+"/GB");assert(list.ok && list.entries.size()==1 && list.entries[0].name=="game.gb");
  assert(prepare(s) && s.exists("/GAMEBOY/DMG.ROM") && s.exists("/GAMEBOY/GAMES/root.GB"));
  assert(parent(std::string(kRoot)+"/GB")==kRoot && parent(kRoot)==kRoot);
  assert(child(kRoot,"..")==kRoot && child(kRoot,"../outside")==kRoot);
  assert(kindOf("Test.Gb")==Kind::gb && kindOf("Test.gbc")==Kind::none);
  assert(navigate(0,14,1,0)==1 && navigate(0,14,-1,0)==13);
  assert(navigate(1,14,0,1)==7 && navigate(7,14,0,1)==13 && navigate(13,14,0,-1)==7);
  for(unsigned n=0;n<140;++n) put(std::string(kRoot)+"/GB/"+std::to_string(n)+".gb");
  list=gameboy_files::list(s,std::string(kRoot)+"/GB");assert(list.more && list.entries.size()==128);
  puts("OK SD folder creation, root/nested GB files, hidden files, saves, sorting and pagination");
}
int main(int argc,char **argv) {
  assert(argc>=2);const fs::path out=argv[1];fs::create_directories(out);
  cartridgeChecks();bootAndCore(out);scaling();joypad();alternateRendering();persistence();
  audioQueue();interrupts();apuLevels();apuTiming();audioFilter();files(out);
  puts("All host checks passed (AddressSanitizer + UndefinedBehaviorSanitizer). Synthetic media only.");
}
