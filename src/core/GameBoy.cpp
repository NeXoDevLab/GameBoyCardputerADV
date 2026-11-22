#include "GameBoy.h"
#include "Config.h"
#include "AudioFilter.h"
#include <algorithm>
#include <csetjmp>
#include <cstring>
#include <new>
#ifdef ESP_PLATFORM
#include <esp_timer.h>
#endif
extern "C" {
#define PEANUT_GB_HEADER_ONLY
#include "../vendor/peanut_gb.h"
#include "../vendor/minigb_apu.h"
}
static minigb_apu_ctx *activeApu=nullptr;
static gb_s *audioCpu=nullptr;
static int16_t *audioPcm=nullptr;
static unsigned audioUsed=0;
static uint32_t audioRenderUs=0;
static void renderAudio(unsigned target) {
  if(target<=audioUsed) return;
#ifdef ESP_PLATFORM
  const int64_t started=esp_timer_get_time();
#endif
  minigb_apu_audio_render(activeApu,audioPcm+2*audioUsed,target-audioUsed);
  audioUsed=target;
#ifdef ESP_PLATFORM
  audioRenderUs+=uint32_t(esp_timer_get_time()-started);
#endif
}
static void syncAudio() {
  if(audioCpu) renderAudio(std::min(unsigned(AUDIO_SAMPLES),
    unsigned(uint64_t(audioCpu->audio_cycles)*AUDIO_SAMPLES/gameboy::kFrameCycles)));
}
extern "C" uint8_t audio_read(uint16_t addr) {
  syncAudio();return activeApu?minigb_apu_audio_read(activeApu,addr):0xff;
}
extern "C" void audio_write(uint16_t addr,uint8_t value) {
  syncAudio();if(activeApu) minigb_apu_audio_write(activeApu,addr,value);
}

namespace gameboy {
static_assert(AUDIO_SAMPLES==kSamples*2,"Audio configuration mismatch");
struct Machine::Impl {
  gb_s gb{};
  minigb_apu_ctx apu{};
  Cartridge *cart=nullptr;
  std::unique_ptr<uint8_t[]> ram;
  uint8_t boot[256]{},pixels[kWidth*kHeight]{};
  int16_t stereo[AUDIO_SAMPLES*2]{},mono[kSamples]{};
  gameboy_audio::LowPass2x lowPass;
  size_t ramBytes=0;
  uint32_t frames=0;
  uint32_t coreUs=0,apuUs=0;
  int32_t previousInput=0,highPassQ8=0;
  bool bootLoaded=false,running=false,dirty=false,inFrame=false;
  const char *error=nullptr;
  std::jmp_buf escape;
  static Impl &get(gb_s *gb) {return *static_cast<Impl*>(gb->direct.priv);}
  void fail(const char *text) {
    error=text;running=false;
    if(inFrame) std::longjmp(escape,1);
    // Upstream error callbacks must not return into PGB_UNREACHABLE.
    std::abort();
  }
  static uint8_t read(gb_s *gb,uint_fast32_t addr) {
    auto &m=get(gb);uint8_t value;
    if(!m.cart->read(addr,value)) m.fail("Error de lectura del cartucho en SD");
    return value;
  }
  static uint8_t ramRead(gb_s *gb,uint_fast32_t addr) {auto &m=get(gb);return addr<m.ramBytes?m.ram[addr]:0xff;}
  static void ramWrite(gb_s *gb,uint_fast32_t addr,uint8_t value) {
    auto &m=get(gb);if(addr<m.ramBytes && m.ram[addr]!=value) {m.ram[addr]=value;m.dirty=true;}
  }
  static uint8_t bootRead(gb_s *gb,uint_fast16_t addr) {return get(gb).boot[addr&255];}
  static void fault(gb_s *gb,gb_error_e,uint16_t) {get(gb).fail("El nucleo no puede ejecutar este cartucho");}
  static void line(gb_s *gb,const uint8_t *pixels,uint_fast8_t y) {
    // Hardware palette values: 0=white, 3=black. Internal buffer: 0=dark.
    if(y<kHeight) {auto *out=get(gb).pixels+y*kWidth;for(unsigned x=0;x<kWidth;++x) out[x]=3-(pixels[x]&3);}
  }
  bool initialize(bool original,const char *&outError) {
    error=nullptr;running=false;inFrame=false;
    if(!cart) {outError="No hay cartucho";return false;}
    if(original && !bootLoaded) {outError="Falta /GAMEBOY/DMG.ROM (256 bytes)";return false;}
    gb={};apu={};activeApu=&apu;minigb_apu_audio_init(&apu);
    // Cache I/O can fail even before CPU execution. Guard initialization too.
    if(setjmp(escape)) {inFrame=false;activeApu=nullptr;outError=error;return false;}
    inFrame=true;
    const auto status=gb_init(&gb,read,ramRead,ramWrite,fault,this);
    if(status!=GB_INIT_NO_ERROR) {inFrame=false;activeApu=nullptr;outError="Cabecera o controlador no compatible";return false;}
    gb_init_lcd(&gb,line);
    if(original) {
      gb_set_bootrom(&gb,bootRead);gb_reset(&gb);
      minigb_apu_audio_write(&apu,0xff26,0); // powered off before boot code
    }
    inFrame=false;activeApu=nullptr;
    std::fill(std::begin(pixels),std::end(pixels),3);
    std::fill(std::begin(mono),std::end(mono),0);
    frames=0;previousInput=highPassQ8=0;lowPass.reset();running=true;outError=nullptr;return true;
  }
};
Machine::Machine():impl_(new(std::nothrow) Impl) {}
Machine::~Machine()=default;
bool Machine::available() const {return bool(impl_);}
bool Machine::start(Cartridge &cart,const uint8_t *boot,size_t bootBytes,bool original,const char *&error) {
  if(!impl_) {error="Sin memoria para Game Boy";return false;}
  stop();auto &m=*impl_;m.cart=&cart;m.bootLoaded=boot && bootBytes==256;
  if(m.bootLoaded) std::memcpy(m.boot,boot,256);
  m.ramBytes=cart.header().ramBytes;
  if(m.ramBytes) {
    m.ram.reset(new(std::nothrow) uint8_t[m.ramBytes]);
    if(!m.ram) {m.ramBytes=0;error="Sin memoria para RAM de cartucho";return false;}
    std::memset(m.ram.get(),0xff,m.ramBytes);
  }
  m.dirty=false;return m.initialize(original,error);
}
bool Machine::reset(bool original,const char *&error) {if(!impl_) {error="Sin memoria";return false;}return impl_->initialize(original,error);}
void Machine::stop() {if(impl_) {impl_->running=false;impl_->ram.reset();impl_->ramBytes=0;impl_->cart=nullptr;impl_->dirty=false;}}
bool Machine::runFrame(bool render) {
  if(!running()) return false;auto &m=*impl_;
  if(setjmp(m.escape)) {m.inFrame=false;audioCpu=nullptr;activeApu=nullptr;return false;}
  m.inFrame=true;activeApu=&m.apu;
  // The platform displays alternate frames. Skip only pixel construction;
  // CPU, interrupts, LCD timing and audio still advance on every frame.
  m.gb.direct.frame_skip=!render;m.gb.display.frame_skip_count=0;
  audioCpu=&m.gb;audioCpu->audio_cycles=0;audioPcm=m.stereo;audioUsed=audioRenderUs=0;
#ifdef ESP_PLATFORM
  const int64_t started=esp_timer_get_time();
#endif
  gb_run_frame(&m.gb);
#ifdef ESP_PLATFORM
  const int64_t coreDone=esp_timer_get_time();m.coreUs=uint32_t(coreDone-started)-audioRenderUs;
#endif
  renderAudio(AUDIO_SAMPLES);audioCpu=nullptr;
#ifdef ESP_PLATFORM
  m.apuUs=audioRenderUs;
#endif
  for(unsigned i=0;i<AUDIO_SAMPLES;++i) {
    const int32_t input=(int32_t(m.stereo[2*i])+m.stereo[2*i+1])/2;
    m.lowPass.push(int16_t(input));
    if(!(i&1)) continue;
    const int32_t filtered=m.lowPass.sample();
    // Continuous ~20 Hz high-pass: keep DAC/DC steps out of the loudspeaker.
    m.highPassQ8+=(filtered-m.previousInput)*256-m.highPassQ8/256;m.previousInput=filtered;
    m.mono[i/2]=int16_t(std::max<int32_t>(-32768,std::min<int32_t>(32767,m.highPassQ8/256)));
  }
  if(!(m.gb.hram_io[0x40]&0x80)) std::fill(std::begin(m.pixels),std::end(m.pixels),3);
  ++m.frames;m.inFrame=false;activeApu=nullptr;return true;
}
void Machine::buttons(uint8_t pressed) {if(impl_) gb_set_joypad(&impl_->gb,uint8_t(~pressed));}
const uint8_t *Machine::pixels() const {return impl_?impl_->pixels:nullptr;}
const int16_t *Machine::audio() const {return impl_?impl_->mono:nullptr;}
uint8_t *Machine::ram() {return impl_?impl_->ram.get():nullptr;}
size_t Machine::ramSize() const {return impl_?impl_->ramBytes:0;}
bool Machine::dirty() const {return impl_ && impl_->dirty;}
void Machine::saved() {if(impl_) impl_->dirty=false;}
bool Machine::running() const {return impl_ && impl_->running;}
bool Machine::booting() const {return running() && !impl_->gb.hram_io[0x50];}
uint16_t Machine::pc() const {return impl_?impl_->gb.cpu_reg.pc.reg:0;}
uint8_t Machine::peek(uint16_t address) const {
  if(!impl_) return 0xff;
  const auto &gb=impl_->gb;
  if(address>=0xff00) return gb.hram_io[address&255];
  if(address>=0xc000 && address<0xe000) return gb.wram[address-0xc000];
  if(address>=0x8000 && address<0xa000) return gb.vram[address-0x8000];
  return 0xff;
}
uint32_t Machine::cpuFlags() const {return impl_?(impl_->gb.gb_ime?1u:0u)|(impl_->gb.gb_halt?2u:0u):0;}
uint32_t Machine::frames() const {return impl_?impl_->frames:0;}
uint32_t Machine::coreMicros() const {return impl_?impl_->coreUs:0;}
uint32_t Machine::apuMicros() const {return impl_?impl_->apuUs:0;}
const char *Machine::error() const {return impl_?impl_->error:"Sin memoria para Game Boy";}

void renderRow(const uint8_t *pixels,unsigned y,uint16_t *out,bool wide,bool grey) {
  std::fill(out,out+240,0);if(!pixels || y>=135) return;
  // Internal shade 0 is darkest, 3 lightest. Filter RGB channels by pixel area
  // to preserve fine strokes when fitting 160x144 into 150x135.
  constexpr uint16_t green[]={0x09c4,0x32a5,0x8c91,0xc6d5};
  constexpr uint16_t grayscale[]={0x0000,0x52aa,0xad55,0xffff};
  const auto *pal=grey?grayscale:green;
  const unsigned sy=y*16/15,fy=y%15,wy0=15-fy,wy1=1+fy;
  const auto *top=pixels+sy*160,*bottom=top+160;
  const auto blend=[&](uint8_t a,uint8_t b,uint8_t c,uint8_t d,unsigned wx0,unsigned wx1) {
    a&=3;b&=3;c&=3;d&=3;
    if(a==b && a==c && a==d) return pal[a];
    const uint16_t ca=pal[a],cb=pal[b],cc=pal[c],cd=pal[d];
    const unsigned wa=wx0*wy0,wb=wx1*wy0,wc=wx0*wy1,wd=wx1*wy1;
    const unsigned r=(((ca>>11)&31)*wa+((cb>>11)&31)*wb+((cc>>11)&31)*wc+((cd>>11)&31)*wd+128)>>8;
    const unsigned g=(((ca>>5)&63)*wa+((cb>>5)&63)*wb+((cc>>5)&63)*wc+((cd>>5)&63)*wd+128)>>8;
    const unsigned bvalue=((ca&31)*wa+(cb&31)*wb+(cc&31)*wc+(cd&31)*wd+128)>>8;
    return uint16_t(r<<11|g<<5|bvalue);
  };
  if(!wide) {
    for(unsigned x=0;x<150;++x) {
      const unsigned sx=x*16/15,fx=x%15;
      out[45+x]=blend(top[sx],top[sx+1],bottom[sx],bottom[sx+1],15-fx,1+fx);
    }
  } else {
    // Horizontal 160 -> 240 consists of [pixel, half-and-half, pixel].
    for(unsigned x=0;x<240;++x) {
      const unsigned sx=(x/3)*2,phase=x%3;
      const unsigned left=sx+(phase==2),right=phase==1?sx+1:left;
      out[x]=blend(top[left],top[right],bottom[left],bottom[right],8,8);
    }
  }
}
}
