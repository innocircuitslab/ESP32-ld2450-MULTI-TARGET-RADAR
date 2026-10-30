#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <math.h>

// PC Dashboard wiring — unchanged
#define TFT_CS    8
#define TFT_RST   9
#define TFT_DC    10
#define TFT_MOSI  11
#define TFT_SCLK  12
#define TFT_BL    13

// Existing rotary encoder
#define ENC_A     4
#define ENC_B     5
#define ENC_SW    6

// LD2450
#define RADAR_RX  1 // receives from LD2450 TX
#define RADAR_TX  2 // transmits to LD2450 RX

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
HardwareSerial radar(1);
GFXcanvas16 canvas(320, 240);

const uint16_t BG = 0x0863;
const uint16_t PANEL = 0x10E5;
const uint16_t GRID = 0x2B4B;
const uint16_t WHITE_C = 0xFFFF;
const uint16_t GREY_C = 0x9CD3;
const uint16_t GREEN_C = 0x05EC;
const uint16_t ORANGE_C = 0xFBE0;
const uint16_t CYAN_C = 0x067F;
const uint16_t PURPLE_C = 0xB17F;
const uint16_t targetColors[3] = {CYAN_C, ORANGE_C, PURPLE_C};

struct Target {
  int16_t x=0, y=0, speed=0;
  float sx=0, sy=0;
  bool valid=false;
  bool hadTarget=false;
};
Target targets[3];

uint8_t radarFrame[30];
uint8_t frameIndex=0;
uint32_t lastFrameMs=0, lastDrawMs=0, lastSerialMs=0;
bool everReceived=false;

const int rangesM[3]={2,3,5};
int rangeIndex=2;  // initial 5 m
bool detailView=false;
const float smoothFactor=0.45f;

// Full quadrature decoder; 4 transitions = one detent on many encoders.
// Some encoder modules have 2 transitions per click: adjust ENC_STEPS if needed.
const int ENC_STEPS=4;
uint8_t lastAB=0;
int8_t encoderAccum=0;
const int8_t TRANSITION[16]={
  0,-1, 1, 0,
  1, 0, 0,-1,
 -1, 0, 0, 1,
  0, 1,-1, 0
};

bool lastRawButton=true, stableButton=true, longHandled=false;
uint32_t buttonChangedMs=0, buttonDownMs=0;

int16_t decodeValue(uint8_t lo,uint8_t hi) {
  uint16_t raw=(uint16_t)lo|((uint16_t)hi<<8);
  int16_t m=raw&0x7FFF;
  return (raw&0x8000)?m:-m;
}

// Accept one full 30-byte target report, with header and footer checks.
bool readRadarFrame() {
  const uint8_t header[4]={0xAA,0xFF,0x03,0x00};
  while(radar.available()) {
    uint8_t b=radar.read();
    if(frameIndex<4) {
      if(b==header[frameIndex]) radarFrame[frameIndex++]=b;
      else {
        frameIndex=(b==header[0])?1:0;
        if(frameIndex) radarFrame[0]=b;
      }
      continue;
    }
    radarFrame[frameIndex++]=b;
    if(frameIndex==30) {
      frameIndex=0;
      if(radarFrame[28]==0x55 && radarFrame[29]==0xCC) return true;
    }
  }
  return false;
}

void decodeTargets() {
  for(int i=0;i<3;i++) {
    int p=4+i*8;
    bool empty=true;
    for(int j=0;j<8;j++) if(radarFrame[p+j]) empty=false;
    Target &t=targets[i];
    t.valid=!empty;
    if(empty) {t.hadTarget=false;continue;}
    t.x=decodeValue(radarFrame[p],radarFrame[p+1]);
    t.y=decodeValue(radarFrame[p+2],radarFrame[p+3]);
    t.speed=decodeValue(radarFrame[p+4],radarFrame[p+5]);
    if(!t.hadTarget) {t.sx=t.x;t.sy=t.y;}
    else {
      t.sx+=smoothFactor*(t.x-t.sx);
      t.sy+=smoothFactor*(t.y-t.sy);
    }
    t.hadTarget=true;
  }
  lastFrameMs=millis();
  everReceived=true;
}

bool online() {return everReceived && (millis()-lastFrameMs < 1500);}
int targetCount() {
  if(!online()) return 0;
  int n=0;
  for(auto &t:targets) if(t.valid) n++;
  return n;
}

void pollEncoder() {
  uint8_t ab=(digitalRead(ENC_A)<<1)|digitalRead(ENC_B);
  if(ab != lastAB) {
    uint8_t idx=(lastAB<<2)|ab;
    encoderAccum+=TRANSITION[idx];
    lastAB=ab;
    if(encoderAccum>=ENC_STEPS) {
      // If your module's direction is reversed, swap ++ and -- here.
      rangeIndex=max(0,rangeIndex-1); // zoom in
      encoderAccum=0;
    } else if(encoderAccum<=-ENC_STEPS) {
      rangeIndex=min(2,rangeIndex+1); // zoom out
      encoderAccum=0;
    }
  }
  bool raw=digitalRead(ENC_SW);
  uint32_t now=millis();
  if(raw!=lastRawButton) {lastRawButton=raw;buttonChangedMs=now;}
  if(now-buttonChangedMs>=25 && raw!=stableButton) {
    stableButton=raw;
    if(!stableButton) {buttonDownMs=now;longHandled=false;}
    else if(!longHandled) detailView=!detailView;
  }
  if(!stableButton && !longHandled && now-buttonDownMs>=1200) {
    rangeIndex=2;
    detailView=false;
    longHandled=true;
  }
}

void drawHeader() {
  canvas.fillRect(0,0,320,29,PANEL);
  canvas.setTextSize(2);
  canvas.setTextColor(WHITE_C);
  canvas.setCursor(8,7);
  canvas.print(detailView?"TARGET DATA":"ESP32 RADAR");
  canvas.fillCircle(243,14,4,online()?GREEN_C:ORANGE_C);
  canvas.setTextSize(1);
  canvas.setCursor(254,11);
  canvas.setTextColor(online()?GREEN_C:ORANGE_C);
  canvas.print(online()?"LIVE":"WAIT");
}

void drawMapView() {
  const int ox=116, oy=204;
  const float scale=164.0f/(rangesM[rangeIndex]*1000.0f);
  canvas.fillRect(0,30,232,185,BG);
  for(int m=1;m<=rangesM[rangeIndex];m++) {
    int y=oy-lroundf(m*1000.0f*scale);
    if(y>=36) {
      canvas.drawFastHLine(8,y,216,GRID);
      canvas.setTextColor(GREY_C);
      canvas.setTextSize(1);
      canvas.setCursor(10,y+2);
      canvas.print(m);canvas.print("m");
    }
  }
  canvas.drawFastVLine(ox,35,171,GRID);
  for(int m=1;m<4;m++) {
    int dx=lroundf(m*1000.0f*scale);
    if(ox+dx<227) canvas.drawFastVLine(ox+dx,35,171,GRID);
    if(ox-dx>5) canvas.drawFastVLine(ox-dx,35,171,GRID);
  }
  canvas.fillCircle(ox,oy,5,WHITE_C);
  canvas.drawCircle(ox,oy,9,GREEN_C);
  if(online()) {
    for(int i=0;i<3;i++) {
      Target &t=targets[i];
      if(!t.valid) continue;
      int x=ox+lroundf(t.sx*scale);
      int y=oy-lroundf(t.sy*scale);
      if(x<16||x>217||y<43||y>195) continue;
      canvas.drawLine(ox,oy,x,y,GRID);
      canvas.drawCircle(x,y,10,targetColors[i]);
      canvas.fillCircle(x,y,5,targetColors[i]);
      int lx=(x>198)?x-20:x+11;
      canvas.setTextSize(1);
      canvas.setTextColor(WHITE_C);
      canvas.setCursor(lx,y-4);
      canvas.print("T");canvas.print(i+1);
    }
  }
  canvas.fillRect(232,30,88,185,PANEL);
  canvas.setTextColor(GREY_C);
  canvas.setTextSize(1);
  canvas.setCursor(241,40);canvas.print("TARGETS");
  canvas.setTextSize(3);
  canvas.setTextColor(WHITE_C);
  canvas.setCursor(243,55);canvas.print(targetCount());
  for(int i=0;i<3;i++) {
    int y=98+i*35;
    canvas.setTextSize(1);
    canvas.setTextColor(targetColors[i]);
    canvas.setCursor(241,y);canvas.print("T");canvas.print(i+1);
    canvas.setCursor(241,y+13);
    if(online() && targets[i].valid) {
      float d=hypotf((float)targets[i].x,(float)targets[i].y)/1000.0f;
      canvas.print(d,2);canvas.print("m");
    } else canvas.print("--");
  }
  canvas.setTextColor(GREEN_C);
  canvas.setCursor(241,205);
  canvas.print(rangesM[rangeIndex]);canvas.print("m RANGE");
}

void drawDetailView() {
  canvas.fillRect(0,30,320,188,BG);
  for(int i=0;i<3;i++) {
    const int y=34+i*60;
    Target &t=targets[i];
    canvas.fillRoundRect(8,y,304,54,5,PANEL);
    canvas.fillCircle(20,y+13,4,targetColors[i]);
    canvas.setTextSize(1);
    canvas.setTextColor(WHITE_C);
    canvas.setCursor(30,y+9);
    canvas.print("TARGET ");canvas.print(i+1);
    if(!online() || !t.valid) {
      canvas.setTextColor(GREY_C);
      canvas.setCursor(20,y+30);canvas.print("NOT DETECTED");
      continue;
    }
    canvas.setTextColor(GREY_C);
    canvas.setCursor(20,y+27);
    canvas.print("X: ");canvas.print(t.x);canvas.print(" mm");
    canvas.setCursor(167,y+27);
    canvas.print("Y: ");canvas.print(t.y);canvas.print(" mm");
    canvas.setCursor(20,y+41);
    canvas.print("DIST: ");
    canvas.print(hypotf((float)t.x,(float)t.y)/1000.0f,2);
    canvas.print(" m");
    canvas.setCursor(167,y+41);
    canvas.print("V: ");canvas.print(t.speed);canvas.print(" cm/s");
  }
}

void drawUI() {
  canvas.fillScreen(BG);
  drawHeader();
  if(detailView) drawDetailView();
  else drawMapView();
  canvas.fillRect(0,222,320,18,PANEL);
  canvas.setTextSize(1);
  canvas.setTextColor(GREY_C);
  canvas.setCursor(8,228);
  canvas.print("LD2450 | ENC:ZOOM / CLICK:VIEW");
  tft.drawRGBBitmap(0,0,canvas.getBuffer(),320,240);
}

void setup() {
  Serial.begin(115200);
  pinMode(ENC_A,INPUT_PULLUP);
  pinMode(ENC_B,INPUT_PULLUP);
  pinMode(ENC_SW,INPUT_PULLUP);
  lastAB=(digitalRead(ENC_A)<<1)|digitalRead(ENC_B);
  lastRawButton=stableButton=digitalRead(ENC_SW);

  pinMode(TFT_BL,OUTPUT);
  digitalWrite(TFT_BL,HIGH);
  SPI.begin(TFT_SCLK,-1,TFT_MOSI,TFT_CS);
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(BG);
  if(!canvas.getBuffer()) {
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(8,90);
    tft.print("Framebuffer failed");
    return;
  }
  radar.setRxBufferSize(1024);
  radar.begin(256000,SERIAL_8N1,RADAR_RX,RADAR_TX);
  Serial.println("Radar UI V3 started");
  drawUI();
}

void loop() {
  if(!canvas.getBuffer()) return;
  pollEncoder();
  if(readRadarFrame()) decodeTargets();
  uint32_t now=millis();
  if(!online()) {
    for(auto &t:targets) {t.valid=false;t.hadTarget=false;}
  }
  if(now-lastDrawMs>=125) {
    lastDrawMs=now;
    drawUI();
  }
  if(now-lastSerialMs>=500) {
    lastSerialMs=now;
    Serial.printf("Targets: %d | Range: %dm | View: %s\n",
      targetCount(),rangesM[rangeIndex],detailView?"DETAIL":"RADAR");
  }
}
