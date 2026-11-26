#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SD_MMC.h>
#include <Adafruit_ILI9341.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <AudioFileSourceFS.h>
#include <vector>

Adafruit_ILI9341 lcd(&SPI,46,10,-1);
WebServer web(80);
AudioGeneratorMP3 mp3;
AudioOutputI2S out;
AudioFileSourceFS source(SD_MMC);
std::vector<String> songs;
File upload;
String destination, password;
const char *status = "Ready";
constexpr char temp[] = "/.upload.part";
int selected=0, volume=2;
bool card=false, codec=false, playing=false, receiving=false, dirty=true, ok=false;

bool reg(uint8_t r,uint8_t v){Wire.beginTransmission(0x18);Wire.write(r);Wire.write(v);return Wire.endTransmission()==0;}
bool setupCodec(){
  // ES8311 slave, external 256fs MCLK, Philips I2S, 16-bit DAC data.
  const uint8_t seq[][2]={{0x44,8},{1,0x30},{2,0},{3,0x10},{4,0x10},{5,0},{6,3},{7,0},{8,0xff},{0x0b,0},{0x0c,0},{0x10,0x1f},{0x11,0x7f},{0,0x80},{1,0x3f},{9,0x0c},{0x13,0x10},{0x0e,2},{0x12,0},{0x0d,1},{0x37,8},{0x45,0},{0x32,0xbf}};
  for(auto &p:seq) if(!reg(p[0],p[1])) return false;
  return true;
}
void scan(){
  songs.clear(); File root=SD_MMC.open("/");
  for(File f=root.openNextFile();f && songs.size()<200;f=root.openNextFile()){
    String path=f.path(), lower=path; lower.toLowerCase();
    if(!f.isDirectory() && lower.endsWith(".mp3")) songs.push_back(path);
  }
  selected=0;dirty=true;
}
void stop(){digitalWrite(1,HIGH);if(mp3.isRunning())mp3.stop();source.close();playing=false;dirty=true;}
void play(){
  stop(); if(!codec || songs.empty() || receiving)return;
  if(source.open(songs[selected].c_str()) && mp3.begin(&source,&out)){
    digitalWrite(1,LOW);playing=true;status="Playing";
  }else{stop();status="Can't play this file";}
}
void button(int x,int y,int w,const char *text){lcd.fillRoundRect(x,y,w,43,6,0x24b5);lcd.setCursor(x+8,y+14);lcd.print(text);}
void draw(){
  dirty=false;lcd.fillScreen(0x0863);lcd.setTextWrap(false);lcd.setTextColor(ILI9341_WHITE);lcd.setTextSize(2);
  lcd.setCursor(12,12);lcd.print("SD MUSIC PLAYER");
  lcd.setTextSize(1);lcd.setCursor(12,48);lcd.print("Wi-Fi: ES3C28P-Music");
  lcd.setCursor(12,63);lcd.print("Password: "+password);
  lcd.setCursor(12,78);lcd.print("Upload: http://192.168.4.1");
  lcd.setCursor(12,110);lcd.print(status);
  if(!songs.empty()){
    lcd.setCursor(12,130);lcd.printf("Track %d / %u",selected+1,(unsigned)songs.size());
    lcd.setTextSize(2);lcd.setCursor(12,151);lcd.print(songs[selected].substring(1,19));
  }
  lcd.setTextSize(2);button(8,192,65,"Prev");button(80,192,80,playing?"Stop":"Play");button(167,192,65,"Next");
  button(8,253,65,"Vol-");button(167,253,65,"Vol+");lcd.setCursor(94,267);lcd.printf("%d/10",volume);
}
void touch(){
  static bool held=false;
  Wire.beginTransmission(0x38);Wire.write(2);if(Wire.endTransmission(false))return;
  if(Wire.requestFrom(0x38,5)!=5)return;
  int count=Wire.read()&15;int x=(Wire.read()&15)<<8;x|=Wire.read();int y=(Wire.read()&15)<<8;y|=Wire.read();
  bool tap=count&&!held;held=count;if(!tap||receiving||x<8||x>=232)return;
  if(y>=192&&y<235){
    if(x>=80&&x<160){if(playing){stop();status="Stopped";}else play();}
    else if(!songs.empty()&&(x<73||x>=167)){
      bool resume=playing;int n=songs.size();selected=(selected+n+(x<73?-1:1))%n;
      if(resume)play();else status="Ready";
    }
    dirty=true;
  }
  if(y>=253&&y<296&&(x<73||x>=167)){volume=constrain(volume+(x<73?-1:1),0,10);out.SetGain(volume/10.0f);dirty=true;}
}
void handleUpload(){
  HTTPUpload &u=web.upload();
  if(u.status==UPLOAD_FILE_START){
    stop();receiving=true;ok=false;destination="";
    String name=u.filename, ext=name;ext.toLowerCase();
    bool valid=name.length()>4&&name.length()<=100&&ext.endsWith(".mp3")&&name[0]!='.';
    for(char c:name)if((unsigned char)c<32||strchr("/\\:*?\"<>|",c))valid=false;
    if(card&&valid){destination="/"+name;if(!SD_MMC.exists(destination)){SD_MMC.remove(temp);upload=SD_MMC.open(temp,FILE_WRITE);ok=bool(upload);}}
    status="Receiving upload";dirty=true;
  }else if(u.status==UPLOAD_FILE_WRITE){
    if(ok && upload.write(u.buf,u.currentSize)!=u.currentSize)ok=false;
  }else if(u.status==UPLOAD_FILE_END||u.status==UPLOAD_FILE_ABORTED){
    if(upload)upload.close();
    ok=ok&&u.status==UPLOAD_FILE_END&&u.totalSize>0&&SD_MMC.rename(temp,destination);
    if(card&&!ok)SD_MMC.remove(temp);
    receiving=false;status=ok?"Upload saved":"Upload failed";dirty=true;if(ok)scan();
  }
}
void setup(){
  pinMode(1,OUTPUT);digitalWrite(1,HIGH);pinMode(45,OUTPUT);digitalWrite(45,LOW);
  SPI.begin(12,13,11,10);lcd.begin(27000000);lcd.setRotation(0);lcd.invertDisplay(true);
  pinMode(18,OUTPUT);digitalWrite(18,LOW);delay(10);digitalWrite(18,HIGH);delay(200);
  Wire.begin(16,15,100000);codec=setupCodec();out.SetPinout(5,7,6,4);out.SetGain(0.2);
  SD_MMC.setPins(38,40,39);card=SD_MMC.begin("/sdcard",true);
  if(card){SD_MMC.remove(temp);scan();}
  status=!card?"Insert FAT32 card and restart":!codec?"Audio codec not found":songs.empty()?"Upload an MP3 to begin":"Ready";
  WiFi.mode(WIFI_AP);password=String(esp_random(),HEX)+String(esp_random(),HEX);password= password.substring(0,12);
  while(password.length()<8)password+='0';
  WiFi.softAP("ES3C28P-Music",password.c_str(),1,0,1);
  web.on("/",HTTP_GET,[]{web.send(200,"text/html","<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'><title>Music upload</title><h1>ES3C28P MP3 upload</h1><p>Choose one MP3. Uploading stops playback.</p><form action=/upload method=post enctype=multipart/form-data><input type=file name=music accept=.mp3 required><button>Upload</button></form>");});
  web.on("/upload",HTTP_POST,[]{web.send(ok?200:400,"text/html",ok?"Saved to microSD. <a href=/>Upload another</a>":"Not saved. Check the card, free space, .mp3 extension, and use a unique file name. <a href=/>Back</a>");ok=false;},handleUpload);
  web.begin();draw();digitalWrite(45,HIGH);
}
void loop(){
  if(playing && !mp3.loop()){stop();status="Track ended";}
  web.handleClient();static uint32_t last=0;
  if(millis()-last>=25){last=millis();touch();}
  if(dirty)draw();
  delay(1);
}