#include "CardputerPlatform.h"
#include "GameFiles.h"
#include "AudioQueue.h"
#include "SaveStore.h"
#include "Settings.h"
#include "../core/GameBoy.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <MD5Builder.h>
#include <driver/i2s_std.h>
#include <algorithm>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace {
using namespace gameboy;
constexpr const char *kRoot=gameboy_files::kRoot;
Machine machine;
Cartridge cartridge;
uint8_t bootRom[256]{};
bool bootLoaded=false,ready=false,original=true,wide=false,grey=false,lastTab=false;
uint8_t volume=77;
uint8_t brightness=180;
gameboy_settings::Settings preferences;
bool settingsDirty=false;
uint32_t volumeAt=0,deadline=0,frameRemainder=0,saveAt=0,noticeUntil=0;
uint32_t cpuUs=0,cpuMaxUs=0,sdReads=0,sdMaxUs=0,paceTimeouts=0;
String folder=kRoot,currentPath,notice;
bool recording=false;
bool menuActive=false,menuExit=false;
bool menuHelp=false,menuRefresh=false;
void serialCommands();
uint32_t recordSequence=0;
uint8_t usbButtons=0,usbButtonFrames=0;

struct SdStorage final : gameboy_files::Storage {
  bool scan(const char *path,gameboy_files::Visitor visitor,void *ctx) override {
    File dir=SD.open(path);if(!dir || !dir.isDirectory()) return false;
    for(;;) {File item=dir.openNextFile();if(!item) break;
      String name=item.name();bool directory=item.isDirectory();item.close();visitor(ctx,name.c_str(),directory);}
    return true;
  }
  bool exists(const char *path) override {return SD.exists(path);}
  bool directory(const char *path) override {File f=SD.open(path);return f && f.isDirectory();}
  bool mkdir(const char *path) override {return SD.mkdir(path);}
  bool move(const char *a,const char *b) override {return !SD.exists(b) && SD.rename(a,b);}
} storage;

struct SdSaveStorage final : gameboy_save::Storage {
  bool exists(const char *path) override {return SD.exists(path);}
  bool remove(const char *path) override {return SD.remove(path);}
  bool move(const char *from,const char *to) override {return !SD.exists(to) && SD.rename(from,to);}
  bool write(const char *path,const uint8_t *data,size_t bytes) override {
    File file=SD.open(path,FILE_WRITE);if(!file) return false;
    const bool ok=file.write(data,bytes)==bytes;file.flush();file.close();return ok;
  }
  bool matches(const char *path,const uint8_t *data,size_t bytes) override {
    File file=SD.open(path,FILE_READ);
    if(!file || file.isDirectory() || file.size()!=bytes) return false;
    uint8_t check[512];
    for(size_t at=0;at<bytes;) {
      const size_t count=std::min(sizeof(check),bytes-at);
      if(file.read(check,count)!=count || std::memcmp(check,data+at,count)) return false;
      at+=count;
    }
    return true;
  }
} saveStorage;

struct SdReader final : Reader {
  File file;uint32_t length=0;
  bool open(const String &path) {
    close();file=SD.open(path,FILE_READ);
    if(!file || file.isDirectory()) {close();return false;}
    length=file.size();return true;
  }
  void close() {file.close();length=0;}
  uint32_t size() const override {return length;}
  bool readAt(uint32_t at,uint8_t *out,size_t count) override {
    if(at>length || count>length-at) return false;
    const uint32_t start=micros();
    bool ok=(file.position()==at || file.seek(at)) && file.read(out,count)==count;
    ++sdReads;sdMaxUs=std::max(sdMaxUs,uint32_t(micros()-start));return ok;
  }
} cartFile;

uint8_t picture[kWidth*kHeight]{};
bool pictureWide=false,pictureGrey=false,pictureClear=true;
std::atomic<bool> lcdBusy{false},audioActive{false};
TaskHandle_t lcdTask=nullptr,audioTask=nullptr;
SemaphoreHandle_t audioMutex=nullptr;
gameboy_audio::Queue queue;
std::atomic<uint32_t> lcdFrames{0},lcdUs{0},audioPackets{0},underruns{0},audioErrors{0};
std::atomic<uint32_t> audioMaxGapUs{0};
int16_t speakerMono[kSamples]{},speakerStereo[kSamples*2]{};
i2s_chan_handle_t speakerTx=nullptr;
std::atomic<uint8_t> outputVolume{77};
bool audioStarted=false;
uint32_t emptyAt=0;
uint32_t lastAudioSubmit=0;

void waitDisplay() {while(lcdBusy.load(std::memory_order_acquire)) delay(1);}
void notify(const String &text) {notice=text;noticeUntil=millis()+2200;if(!recording) Serial.println(text);}
void loadSettings() {
  const String main=gameboy_settings::kPath,backup=main+".bak";
  for(const String &path:{main,backup}) {
    File file=SD.open(path,FILE_READ);
    if(!file || file.isDirectory() || file.size()>gameboy_settings::kMaxBytes) continue;
    std::string text(file.size(),'\0');
    if(file.read(reinterpret_cast<uint8_t*>(text.data()),text.size())!=text.size() ||
       !gameboy_settings::decode(text,preferences)) continue;
    break;
  }
  volume=preferences.volume;brightness=preferences.brightness;
  wide=preferences.wide;grey=preferences.grey;original=preferences.original;
  outputVolume.store(volume);M5.Display.setBrightness(brightness);
  if(!preferences.lastGame.empty()) {
    const auto remembered=gameboy_files::parent(preferences.lastGame);
    if(storage.directory(remembered.c_str())) folder=remembered.c_str();
  }
}
bool persistSettings() {
  if(!settingsDirty) return true;
  preferences.volume=volume;preferences.brightness=brightness;
  preferences.wide=wide;preferences.grey=grey;preferences.original=original;
  const auto text=gameboy_settings::encode(preferences);
  if(!gameboy_save::commit(saveStorage,gameboy_settings::kPath,
      reinterpret_cast<const uint8_t*>(text.data()),text.size())) {
    notify("No se pudieron guardar los ajustes");return false;
  }
  settingsDirty=false;return true;
}
void silence() {
  audioActive.store(false,std::memory_order_release);
  if(audioMutex) xSemaphoreTake(audioMutex,portMAX_DELAY);
  queue.clear();audioStarted=false;emptyAt=0;lastAudioSubmit=0;
  if(audioMutex) xSemaphoreGive(audioMutex);
}
bool beginNativeAudio() {
  // Initialize the ADV ES8311 directly. Starting/stopping M5's asynchronous
  // playback engine here races its teardown on this ESP-IDF version.
  static constexpr uint8_t codec[][2]={
    {0x00,0x80},{0x01,0xb5},{0x02,0x18},{0x0d,0x01},
    {0x12,0x00},{0x13,0x10},{0x32,0xbf},{0x37,0x08}
  };
  for(const auto &reg:codec) if(!M5.In_I2C.writeRegister8(0x18,reg[0],reg[1],100000)) return false;
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,I2S_ROLE_MASTER);
  channel.auto_clear=true;
  channel.dma_desc_num=4;channel.dma_frame_num=256;
  if(i2s_new_channel(&channel,&speakerTx,nullptr)!=ESP_OK) return false;
  i2s_std_config_t config{};
  config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
  config.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
  config.gpio_cfg.mclk=I2S_GPIO_UNUSED;
  config.gpio_cfg.bclk=GPIO_NUM_41;config.gpio_cfg.ws=GPIO_NUM_43;
  config.gpio_cfg.dout=GPIO_NUM_42;config.gpio_cfg.din=I2S_GPIO_UNUSED;
  if(i2s_channel_init_std_mode(speakerTx,&config)!=ESP_OK || i2s_channel_enable(speakerTx)!=ESP_OK) {
    i2s_del_channel(speakerTx);speakerTx=nullptr;return false;
  }
  return true;
}
void audioWorker(void*) {
  for(;;) {
    xSemaphoreTake(audioMutex,portMAX_DELAY);
    bool content=false;
    if(audioActive.load(std::memory_order_acquire)) {
      if(!audioStarted && queue.packets()>=3) audioStarted=true;
      if(audioStarted) {
        content=queue.pop(speakerMono);
        if(!content) {++underruns;audioStarted=false;lastAudioSubmit=0;}
      }
    }
    const int gain=outputVolume.load(std::memory_order_relaxed);
    for(unsigned i=0;i<kSamples;++i) {
      // APU has 6 dB mixing headroom. Recover it before the user's gain.
      const int sample=content?int(speakerMono[i])*gain*2/255:0;
      const int16_t value=int16_t(std::max(-32768,std::min(32767,sample)));
      speakerStereo[2*i]=speakerStereo[2*i+1]=value;
    }
    size_t written=0;
    const auto result=i2s_channel_write(speakerTx,speakerStereo,sizeof(speakerStereo),&written,200);
    if(result!=ESP_OK || written!=sizeof(speakerStereo)) {++audioErrors;audioStarted=false;}
    else if(content) {
      const uint32_t now=micros();
      if(lastAudioSubmit) audioMaxGapUs.store(std::max(audioMaxGapUs.load(),uint32_t(now-lastAudioSubmit)));
      lastAudioSubmit=now;++audioPackets;
    }
    // Inactive/empty input is explicit zero PCM. DMA can never loop an old
    // sound block while the emulator is paused or refilling its reserve.
    xSemaphoreGive(audioMutex);
    vTaskDelay(result==ESP_OK?1:10);
  }
}
void drawPicture() {
  static uint16_t tile[240*4],active[150*4];
  static bool lastWide=false,initialized=false;
  const uint32_t start=micros();
  if(pictureClear || !initialized || lastWide!=pictureWide) M5.Display.fillScreen(TFT_BLACK);
  initialized=true;lastWide=pictureWide;
  for(unsigned y=0;y<135;y+=4) {
    const unsigned rows=std::min(4u,135-y);
    for(unsigned row=0;row<rows;++row) renderRow(picture,y+row,tile+240*row,pictureWide,pictureGrey);
    if(pictureWide) M5.Display.pushImage(0,y,240,rows,tile);
    else {
      for(unsigned row=0;row<rows;++row) std::memcpy(active+150*row,tile+240*row+45,150*sizeof(uint16_t));
      M5.Display.pushImage(45,y,150,rows,active);
    }
  }
  lcdUs=micros()-start;++lcdFrames;
}
void lcdWorker(void*) {
  for(;;) {ulTaskNotifyTake(pdTRUE,portMAX_DELAY);drawPicture();lcdBusy.store(false,std::memory_order_release);}
}
void present(bool force=false) {
  if(lcdBusy.load(std::memory_order_acquire)) return;
  // CPU and APU always emulate every frame; LCD is limited to ~30 fps.
  if(!force && (machine.frames()&1)) return;
  std::memcpy(picture,machine.pixels(),sizeof(picture));pictureWide=wide;pictureGrey=grey;
  pictureClear=force;
  if(lcdTask) {lcdBusy.store(true,std::memory_order_release);xTaskNotifyGive(lcdTask);} else drawPicture();
}
String clipped(String text,int width=224) {
  while(text.length() && M5.Display.textWidth(text)>width) {
    unsigned end=text.length()-1;while(end && (uint8_t(text[end])&0xc0)==0x80) --end;text.remove(end);
  }
  return text;
}
void message(const String &title,const String &detail) {
  waitDisplay();M5.Display.fillScreen(TFT_BLACK);M5.Display.fillRect(0,0,240,3,0x8c91);
  M5.Display.setTextColor(0xc6d5,TFT_BLACK);M5.Display.setTextSize(1);
  M5.Display.drawString(clipped(title),6,13,2);
  M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);M5.Display.drawString(clipped(detail),6,47);
  Serial.printf("GB %s: %s\n",title.c_str(),detail.c_str());
}
bool held(char key) {
  return M5Cardputer.Keyboard.isKeyPressed(key) ||
    (key>='a' && key<='z' && M5Cardputer.Keyboard.isKeyPressed(char(key-'a'+'A')));
}
void resetClock() {deadline=micros();frameRemainder=0;}
void stopRecording() {
  if(!recording) return;
  recording=false;usbButtons=usbButtonFrames=0;
  Serial.println("GBREC-END");Serial.setTxTimeoutMs(2);resetClock();
}
bool usbWrite(const uint8_t *bytes,size_t count) {
  while(count) {
    const auto sent=Serial.write(bytes,count);
    if(!sent) {stopRecording();return false;}
    bytes+=sent;count-=sent;
  }
  return true;
}
void recordFrame() {
  // Every CPU frame includes audio; alternate frames include a packed 2-bit
  // native image. The host reconstructs timing from the emulated clock.
  const bool video=!(recordSequence&1);
  uint8_t header[16]={'G','B','C','R'};
  auto little=[](uint8_t *p,uint32_t value,unsigned n) {for(unsigned i=0;i<n;++i) p[i]=value>>(i*8);};
  little(header+4,recordSequence,4);little(header+8,kSamples,2);
  little(header+10,(video?1:0)|(machine.booting()?2:0),2);little(header+12,video?kWidth*kHeight/4:0,4);
  if(!usbWrite(header,sizeof(header)) || !usbWrite(reinterpret_cast<const uint8_t*>(machine.audio()),kSamples*2)) return;
  if(video) {
    uint8_t packed[320];const auto *pixels=machine.pixels();
    for(unsigned base=0;base<kWidth*kHeight;base+=sizeof(packed)*4) {
      for(unsigned i=0;i<sizeof(packed);++i) {
        const auto *p=pixels+base+4*i;packed[i]=p[0]|p[1]<<2|p[2]<<4|p[3]<<6;
      }
      if(!usbWrite(packed,sizeof(packed))) return;
    }
  }
  ++recordSequence;
}
String savePath() {return currentPath.substring(0,currentPath.lastIndexOf('.'))+".sav";}

bool saveRam() {
  if(!machine.dirty() || !machine.ramSize() || !cartridge.header().battery || currentPath.isEmpty()) return true;
  const String path=savePath();
  if(!gameboy_save::commit(saveStorage,path.c_str(),machine.ram(),machine.ramSize())) {
    notify("Error SAV: copia anterior conservada");return false;
  }
  machine.saved();saveAt=millis();Serial.printf("GB SAVED %s bytes=%u\n",path.c_str(),unsigned(machine.ramSize()));return true;
}
bool restoreRam() {
  if(!machine.ramSize() || !cartridge.header().battery) return true;
  const String path=savePath(),backup=path+".bak";
  for(const String &candidate:{path,backup}) {
    if(!SD.exists(candidate)) continue;
    File in=SD.open(candidate,FILE_READ);
    if(in && in.size()==machine.ramSize() && in.read(machine.ram(),machine.ramSize())==machine.ramSize()) {
      machine.saved();Serial.printf("GB SAVE loaded %s\n",candidate.c_str());return true;
    }
  }
  if(SD.exists(path) || SD.exists(backup)) {notify("SAV incompatible: se conserva en SD");return false;}
  return true;
}
bool loadFile(const String &path) {
  // Validate before disturbing the active game or its cartridge reader.
  SdReader candidate;Header header;const char *error=nullptr;
  if(!candidate.open(path) || !inspect(candidate,header,error)) {notify(error?error:"No se puede abrir el cartucho");return false;}
  if(original && !bootLoaded) {notify("Falta /GAMEBOY/DMG.ROM (256 bytes)");return false;}
  if(!saveRam()) return false;
  silence();waitDisplay();machine.stop();cartridge.eject();cartFile.close();
  if(!cartFile.open(path) || !cartridge.mount(cartFile,error) ||
     !machine.start(cartridge,bootLoaded?bootRom:nullptr,bootLoaded?256:0,original,error)) {
    notify(error?error:"No se pudo cargar el cartucho");return false;
  }
  currentPath=path;
  if(!restoreRam()) {machine.stop();return false;}
  folder=gameboy_files::parent(path.c_str()).c_str();
  if(preferences.lastGame!=path.c_str()) {
    preferences.lastGame=path.c_str();settingsDirty=true;
  }
  persistSettings();
  saveAt=millis();resetClock();
  Serial.printf("GB LOADED %s title=%s rom=%u ram=%u original=%u\n",path.c_str(),header.title,header.romBytes,header.ramBytes,original);
  return true;
}
void volumeKeys(bool fn) {
  if(fn && millis()-volumeAt>140) {
    const int delta=(held('=')||held('+'))?13:held('-')?-13:0;
    if(delta) {volume=uint8_t(std::max(0,std::min(255,int(volume)+delta)));outputVolume.store(volume);volumeAt=millis();settingsDirty=true;}
  }
}
using Listing=gameboy_files::Listing;
Listing scan() {
  auto list=gameboy_files::list(storage,folder.c_str());
  if(folder!=kRoot) list.entries.insert(list.entries.begin(),{"[..] Volver",true});
  return list;
}
void drawMenu(const Listing &list,size_t selected) {
  M5.Display.fillScreen(TFT_BLACK);M5.Display.fillRect(0,0,240,3,0x8c91);
  M5.Display.setTextSize(1);M5.Display.setTextColor(0xc6d5,TFT_BLACK);
  M5.Display.drawString("Game Boy DMG",6,5,2);
  if(!list.entries.empty()) M5.Display.drawRightString(String(unsigned(selected+1))+"/"+String(unsigned(list.entries.size())),234,9);
  M5.Display.drawString(clipped(folder),6,24);
  const size_t start=selected/6*6;
  for(size_t i=start;i<list.entries.size() && i<start+6;++i) {
    const int y=38+12*(i-start);const bool chosen=i==selected;
    const uint16_t bg=chosen?0x09c4:TFT_BLACK;
    M5.Display.fillRect(3,y-1,234,12,bg);M5.Display.setTextColor(chosen?0xc6d5:TFT_WHITE,bg);
    const auto &e=list.entries[i];M5.Display.drawString(clipped(String(e.directory?"/ ":"  ")+e.name.c_str()),5,y);
  }
  M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  if(!list.ok) M5.Display.drawString("Carpeta no disponible",6,50);
  else if(list.entries.empty()) M5.Display.drawString("Copia .gb en GAMEBOY/GAMES/GB",6,50);
  if(int32_t(millis()-noticeUntil)<0) M5.Display.drawString(clipped(notice),5,113);
  else M5.Display.drawString(String(original?"O:original ":"R:directo ")+"ROM:"+(bootLoaded?"OK ":"falta ")+"Vol:"+String(unsigned(volume)*100/255)+"%",5,113);
  M5.Display.drawString(list.more?"Usa subcarpetas H:ayuda":"Enter:cargar H:ayuda Tab:salir",5,125);
}
void drawHelp() {
  M5.Display.fillScreen(TFT_BLACK);M5.Display.setTextColor(0xc6d5,TFT_BLACK);
  M5.Display.drawString("Controles",6,5,2);M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);
  const char *rows[]={"Flechas / WASD: direccion","Z / espacio: A   X: B","Enter: Start  Retroceso: Select",
    "Fn+R: reiniciar  Fn+S: guardar","Fn+W: ancho  Fn+P: paleta","Fn+B: brillo  Fn+ +/-: volumen","Selector: O original / R directo"};
  for(unsigned i=0;i<7;++i) M5.Display.drawString(rows[i],5,29+12*i);
  M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);M5.Display.drawString("H / Enter: volver   Tab: salir",5,125);
}
void menu() {
  stopRecording();
  silence();waitDisplay();machine.buttons(0);saveRam();persistSettings();
  menuActive=true;menuExit=false;menuHelp=false;menuRefresh=false;
  auto list=scan();size_t selected=0;
  const String remembered=currentPath.isEmpty()?preferences.lastGame.c_str():currentPath;
  for(size_t i=0;i<list.entries.size();++i)
    if(folder+"/"+list.entries[i].name.c_str()==remembered) {selected=i;break;}
  drawMenu(list,selected);
  bool previousEnter=true,previousTab=true,previousBack=true,previousHelp=false,previousBrightness=false;
  bool &help=menuHelp;
  int previousMove=0;uint32_t moveAt=0;
  while(true) {
    serialCommands();if(menuExit) break;
    if(menuRefresh) {if(help) drawHelp();else drawMenu(list,selected);menuRefresh=false;}
    M5Cardputer.update();const auto &k=M5Cardputer.Keyboard.keysState();
    if((k.tab && !previousTab) || M5.BtnA.wasPressed()) break;previousTab=k.tab;
    const bool helpKey=!k.fn && held('h');
    if((helpKey && !previousHelp) || (help && k.enter && !previousEnter)) {
      help=!help;if(help) drawHelp();else drawMenu(list,selected);
      previousHelp=helpKey;previousEnter=k.enter;delay(5);continue;
    }
    previousHelp=helpKey;
    if(help) {previousEnter=k.enter;delay(5);continue;}
    bool redraw=false;
    const int movement=held(';')?-1:held('.')?1:held(',')?-6:held('/')?6:0;
    if(movement && (movement!=previousMove || millis()-moveAt>180)) {
      selected=gameboy_files::navigate(selected,list.entries.size(),movement==-1||movement==1?movement:0,movement==-6?-1:movement==6?1:0);
      moveAt=millis();redraw=true;
    }
    previousMove=movement;
    if(!k.fn && held('o') && !original) {original=true;redraw=true;settingsDirty=true;}
    if(!k.fn && held('r') && original) {original=false;redraw=true;settingsDirty=true;}
    const uint8_t oldVolume=volume;volumeKeys(k.fn);redraw|=oldVolume!=volume;
    const bool brightnessKey=k.fn && held('b');
    if(brightnessKey && !previousBrightness) {
      brightness=brightness<128?128:brightness<180?180:brightness<255?255:64;
      M5.Display.setBrightness(brightness);settingsDirty=true;
    }
    previousBrightness=brightnessKey;
    const bool back=k.del;
    if(back && !previousBack && folder!=kRoot) {folder=gameboy_files::parent(folder.c_str()).c_str();list=scan();selected=0;redraw=true;}
    previousBack=back;
    if(k.enter && !previousEnter && !list.entries.empty()) {
      const auto entry=list.entries[selected];
      if(entry.directory) {
        folder=(entry.name=="[..] Volver"?gameboy_files::parent(folder.c_str()):gameboy_files::child(folder.c_str(),entry.name)).c_str();
        list=scan();selected=0;redraw=true;
      } else {
        message("Leyendo cartucho...",entry.name.c_str());
        if(loadFile(folder+"/"+entry.name.c_str())) break;
        drawMenu(list,selected);redraw=true;
      }
    }
    previousEnter=k.enter;
    if(redraw) drawMenu(list,selected);delay(5);
  }
  menuActive=false;menuExit=false;
  persistSettings();
  machine.buttons(0);lastTab=true;resetClock();
  if(machine.running()) present(true);else message("Game Boy DMG","Tab: cartuchos de microSD");
}
void resetCartridge() {
  stopRecording();
  if(!machine.running()) return;
  silence();waitDisplay();if(!saveRam()) return;persistSettings();
  const char *error=nullptr;
  if(!machine.reset(original,error)) {notify(error?error:"Error de reinicio");return;}
  resetClock();present(true);
}
void keyboard() {
  static uint32_t scannedAt=0;
  const uint32_t now=micros();const bool scanned=uint32_t(now-scannedAt)>=16000;
  if(scanned) {M5Cardputer.update();scannedAt=now;}
  const auto &k=M5Cardputer.Keyboard.keysState();
  const bool open=k.tab && !lastTab;lastTab=k.tab;
  if(open || (scanned && M5.BtnA.wasPressed())) {menu();return;}
  static bool r=false,s=false,w=false,p=false,b=false;
  const bool nr=k.fn && held('r'),ns=k.fn && held('s'),nw=k.fn && held('w'),np=k.fn && held('p'),nb=k.fn && held('b');
  if(nr && !r) resetCartridge();
  if(ns && !s) {stopRecording();silence();saveRam();persistSettings();resetClock();}
  if(nw && !w) {waitDisplay();wide=!wide;settingsDirty=true;present(true);}
  if(np && !p) {waitDisplay();grey=!grey;settingsDirty=true;present(true);}
  if(nb && !b) {
    brightness=brightness<128?128:brightness<180?180:brightness<255?255:64;
    M5.Display.setBrightness(brightness);settingsDirty=true;
  }
  r=nr;s=ns;w=nw;p=np;b=nb;volumeKeys(k.fn);
  uint8_t pressed=0;
  if(!k.fn) {
    if(held(';')||held('w')) pressed|=Up;
    if(held('.')||held('s')) pressed|=Down;
    if(held(',')||held('a')) pressed|=Left;
    if(held('/')||held('d')) pressed|=Right;
    if(held('z')||held(' ')) pressed|=A;
    if(held('x')) pressed|=B;
    if(k.enter) pressed|=Start;
    if(k.del) pressed|=Select;
  }
  if(usbButtonFrames) {pressed|=usbButtons;--usbButtonFrames;}
  machine.buttons(pressed);
}
void capture() {
  silence();waitDisplay();Serial.setTxTimeoutMs(1000);
  Serial.println("GBFRAME 240 135 RGB565LE");uint16_t line[240];
  for(unsigned y=0;y<135;++y) {
    renderRow(machine.pixels(),y,line,wide,grey);Serial.write(reinterpret_cast<uint8_t*>(line),sizeof(line));
  }
  Serial.println("\nGBFRAME END");Serial.setTxTimeoutMs(2);resetClock();
}
void diagnostics() {
  Serial.printf("GBPROFILE rom=%s core_us=%u apu_us=%u\n",currentPath.c_str(),machine.coreMicros(),machine.apuMicros());
  Serial.println("GBAUDIO oversample=2 fir_taps=31 keyboard_poll_us=16000");
  Serial.printf("GB-ADV V0.6 frames=%u pc=%04x boot=%u original=%u heap=%u cpu_us=%u cpu_max_us=%u lcd=%u lcd_us=%u packets=%u queued=%u underruns=%u dropped=%u errors=%u timeouts=%u sd_reads=%u sd_max_us=%u cache_misses=%u audio_gap_us=%u sample_rate=%u ms=%u lcdc=%02x ly=%u ie=%02x if=%02x ime=%u\n",
    machine.frames(),machine.pc(),machine.booting(),original,ESP.getFreeHeap(),cpuUs,cpuMaxUs,lcdFrames.load(),lcdUs.load(),
    audioPackets.load(),queue.packets(),underruns.load(),queue.dropped(),audioErrors.load(),paceTimeouts,sdReads,sdMaxUs,cartridge.misses(),audioMaxGapUs.load(),kSampleRate,millis(),machine.peek(0xff40),machine.peek(0xff44),machine.peek(0xffff),machine.peek(0xff0f),machine.cpuFlags()&1);
}
void settingsInfo() {
  Serial.printf("GBSET volume=%u brightness=%u wide=%u grey=%u original=%u dirty=%u last=%s\n",
    volume,brightness,wide,grey,original,settingsDirty,preferences.lastGame.c_str());
}
void setOption(const String &request) {
  const int separator=request.indexOf(' ',7);
  if(separator<0) {notify("Formato: option nombre valor");return;}
  const String name=request.substring(7,separator),value=request.substring(separator+1);
  bool valid=value.length()>0 && value.length()<=3;
  for(unsigned i=0;i<value.length();++i) valid&=value[i]>='0' && value[i]<='9';
  const unsigned number=value.toInt();
  if(!valid || number>255 || ((name=="wide" || name=="grey" || name=="original") && number>1) ||
      (name=="brightness" && number<32)) {notify("Opcion no valida");return;}
  if(name=="volume") {volume=number;outputVolume.store(volume);}
  else if(name=="brightness") {brightness=number;M5.Display.setBrightness(brightness);}
  else if(name=="wide") {waitDisplay();wide=number;}
  else if(name=="grey") {waitDisplay();grey=number;}
  else if(name=="original") original=number;
  else {notify("Opcion no valida");return;}
  settingsDirty=true;menuRefresh=true;settingsInfo();
  if(!menuActive && machine.running()) present(true);
}
void listMedia(const String &path,unsigned depth=0) {
  const auto files=gameboy_files::list(storage,path.c_str());
  for(const auto &entry:files.entries) {
    const String full=path+"/"+entry.name.c_str();
    if(entry.directory) {if(depth<6) listMedia(full,depth+1);}
    else Serial.printf("GBFILE %s\n",full.c_str());
  }
}
void bootInfo() {
  MD5Builder md5;md5.begin();md5.add(bootRom,sizeof(bootRom));md5.calculate();
  Serial.printf("GB BOOT path=/GAMEBOY/DMG.ROM loaded=%u bytes=%u md5=%s\n",bootLoaded,bootLoaded?256:0,bootLoaded?md5.toString().c_str():"none");
}
void directoryInfo(const String &path) {
  File dir=SD.open(path,FILE_READ);
  if(!dir || !dir.isDirectory()) {Serial.println("GBDIR ERROR");return;}
  for(;;) {
    File item=dir.openNextFile();if(!item) break;
    Serial.printf("GBDIR %c %u %s\n",item.isDirectory()?'D':'F',unsigned(item.size()),item.name());item.close();
  }
  Serial.println("GBDIR END");
}
bool copySdFile(const String &source,const String &target) {
  if(!source.startsWith("/") || !target.startsWith("/GAMEBOY/") || source.indexOf("/../")>=0 || target.indexOf("/../")>=0 || SD.exists(target)) {
    notify("Copia no valida o destino existente");return false;
  }
  File input=SD.open(source,FILE_READ);if(!input || input.isDirectory()) {notify("Origen no disponible");return false;}
  // Write to a temporary path and publish only after checking every byte again.
  const String temp=target+".copying";
  if(SD.exists(temp)) {notify("Existe una copia temporal previa");return false;}
  File output=SD.open(temp,FILE_WRITE);if(!output) {notify("No se puede crear destino");return false;}
  uint8_t buffer[1024],verify[1024];bool ok=true;const auto bytes=input.size();
  while(input.available()) {
    const auto n=input.read(buffer,sizeof(buffer));
    if(!n || output.write(buffer,n)!=n) {ok=false;break;}
  }
  output.flush();output.close();
  File check=SD.open(temp,FILE_READ);ok=ok && check && check.size()==bytes && input.seek(0);
  while(ok && input.available()) {
    const auto n=input.read(buffer,sizeof(buffer));
    if(!n || check.read(verify,n)!=n || std::memcmp(buffer,verify,n)) ok=false;
  }
  check.close();input.close();
  if(!ok || !SD.rename(temp,target)) {SD.remove(temp);notify("Error de copia; origen conservado");return false;}
  Serial.printf("GBCOPY OK bytes=%u source=%s target=%s\n",unsigned(bytes),source.c_str(),target.c_str());return true;
}
void reloadBoot() {
  File boot=SD.open("/GAMEBOY/DMG.ROM",FILE_READ);
  bootLoaded=boot && !boot.isDirectory() && boot.size()==256 && boot.read(bootRom,256)==256;boot.close();bootInfo();
}
void serialCommands() {
  static String command;
  while(Serial.available()) {
    const char ch=Serial.read();
    if(ch=='\r') continue;
    if(ch!='\n') {if(command.length()<250) command+=ch;continue;}
    // Clear before dispatch: the physical selector also services USB commands.
    const String request=command;command="";
    if(request=="stop") stopRecording();
    else if(request.startsWith("key ")) {usbButtons=uint8_t(request.substring(4).toInt());usbButtonFrames=6;}
    else if(recording) continue;
    else if(request=="?") diagnostics();
    else if(request=="settings") settingsInfo();
    else if(request.startsWith("option ")) setOption(request);
    else if(request=="settings-save") {silence();persistSettings();settingsInfo();menuRefresh=true;}
    else if(request=="help" && menuActive) {menuHelp=!menuHelp;menuRefresh=true;}
    else if(request=="boot") bootInfo();
    else if(request.startsWith("rompeek ")) {
      uint32_t address=strtoul(request.substring(8).c_str(),nullptr,16);uint8_t byte;
      Serial.printf("GBROM %04x",unsigned(address));
      for(unsigned i=0;i<24;++i) {if(!cartridge.read(address+i,byte)) break;Serial.printf(" %02x",byte);}
      Serial.println();
    }
    else if(request.startsWith("mempeek ")) {
      uint16_t address=strtoul(request.substring(8).c_str(),nullptr,16);
      Serial.printf("GBMEM %04x",address);
      for(unsigned i=0;i<24;++i) Serial.printf(" %02x",machine.peek(uint16_t(address+i)));
      Serial.println();
    }
    else if(request=="reloadboot") {silence();reloadBoot();resetClock();}
    else if(request.startsWith("dir ")) {
      silence();Serial.setTxTimeoutMs(100);directoryInfo(request.substring(4));Serial.setTxTimeoutMs(2);resetClock();
    }
    else if(request.startsWith("copy ")) {
      const int separator=request.indexOf('|',5);
      silence();Serial.setTxTimeoutMs(100);
      if(separator>5) copySdFile(request.substring(5,separator),request.substring(separator+1));
      else notify("Formato: copy /origen|/GAMEBOY/destino");
      Serial.setTxTimeoutMs(2);resetClock();
    }
    else if(request=="files") {
      silence();Serial.setTxTimeoutMs(100);listMedia(kRoot);Serial.println("GBFILES END");Serial.setTxTimeoutMs(2);resetClock();
    }
    else if(request=="exit") {if(menuActive) menuExit=true;}
    else if(request=="c") capture();
    else if(request=="r") resetCartridge();
    else if(request=="s") {silence();saveRam();persistSettings();resetClock();}
    else if(request=="m") {if(menuActive) menuExit=true;else menu();}
    else if(request=="record-current" && machine.running()) {
      silence();if(menuActive) menuExit=true;
      recording=true;recordSequence=0;Serial.setTxTimeoutMs(1000);
      Serial.printf("GBREC1 160 144 %u 4194304 70224\n",kSampleRate);
    }
    else if(request.startsWith("load ") || request.startsWith("record ")) {
      const bool record=request.startsWith("record ");const String path=request.substring(record?7:5);
      if(path.startsWith(String(kRoot)+"/") && path.indexOf("/../")<0 && path.indexOf("/./")<0 &&
         gameboy_files::kindOf(path.c_str())==gameboy_files::Kind::gb) {
        const bool previousOriginal=original;if(record) original=true;
        if(loadFile(path)) {
          if(menuActive) menuExit=true;
          if(record) {
            recording=true;recordSequence=0;Serial.setTxTimeoutMs(1000);
            Serial.printf("GBREC1 160 144 %u 4194304 70224\n",kSampleRate);
          }
        }
        if(record) original=previousOriginal;
      }
      else notify("Ruta de cartucho no valida");
    }
  }
}
}
namespace gameboy_platform {
void begin() {
  Serial.begin(115200);Serial.setTxTimeoutMs(2);
  auto config=M5.config();config.clear_display=true;M5Cardputer.begin(config,true);
  M5.Display.setRotation(1);M5.Display.setSwapBytes(true);M5.Display.setBrightness(180);
  message("Game Boy DMG","Montando microSD...");
  if(!machine.available()) {message("Sin memoria","No se pudo crear Game Boy");return;}
  M5.Speaker.end();auto speaker=M5.Speaker.config();speaker.task_priority=4;speaker.task_pinned_core=0;
  speaker.dma_buf_len=128;speaker.dma_buf_count=4;M5.Speaker.config(speaker);
  M5.Speaker.setVolume(volume);M5.Speaker.setChannelVolume(0,255);
  SPI.begin(40,39,14,12);
  if(!SD.begin(12,SPI,25000000)) {message("SD no disponible","Comprueba FAT32 y reinicia");return;}
  if(!gameboy_files::prepare(storage)) {message("Error de carpetas","No se puede crear /GAMEBOY/GAMES/GB");return;}
  loadSettings();
  File boot=SD.open("/GAMEBOY/DMG.ROM",FILE_READ);
  bootLoaded=boot && boot.size()==256 && boot.read(bootRom,256)==256;boot.close();
  bootInfo();
  if(!beginNativeAudio()) {message("Error de audio I2S","No se pudo iniciar el altavoz");return;}
  audioMutex=xSemaphoreCreateMutex();
  if(!audioMutex || xTaskCreatePinnedToCore(audioWorker,"gb_audio",4096,nullptr,3,&audioTask,0)!=pdPASS) {
    message("Sin memoria para audio","Reinicia el Cardputer");return;
  }
  if(xTaskCreatePinnedToCore(lcdWorker,"gb_lcd",4096,nullptr,1,&lcdTask,0)!=pdPASS) lcdTask=nullptr;
  ready=true;resetClock();Serial.printf("GB-ADV V0.6 ready heap=%u PSRAM=%u boot=%u cpu_mhz=%u\n",ESP.getFreeHeap(),ESP.getPsramSize(),bootLoaded,getCpuFrequencyMhz());
  if(!bootLoaded) notify("Original necesita DMG.ROM de 256 bytes");
  menu();
}
void tick() {
  if(!ready) {M5Cardputer.update();delay(20);return;}
  keyboard();serialCommands();
  if(!machine.running()) {delay(10);return;}
  if(recording) {
    if(!machine.runFrame()) {stopRecording();silence();message("Error de emulacion",machine.error());return;}
    recordFrame();present();delay(1);return;
  }
  audioActive.store(true,std::memory_order_release);
  const uint32_t start=micros();
  if(!machine.runFrame(bool(machine.frames()&1))) {silence();message("Error de emulacion",machine.error());return;}
  cpuUs=micros()-start;cpuMaxUs=std::max(cpuMaxUs,cpuUs);
  const uint32_t waitAt=millis();
  while(queue.packets()>=4) {
    delay(1);
    if(millis()-waitAt>150) {++paceTimeouts;silence();break;}
  }
  queue.push(machine.audio());present();
  // The I2S audio clock controls emulation speed. Fill its reserve immediately
  // after a pause or SD stall instead of adding a second independent sleep.
  delay(1);
  if(cartridge.header().battery && machine.dirty() && millis()-saveAt>30000) {silence();saveRam();resetClock();saveAt=millis();}
}
}
