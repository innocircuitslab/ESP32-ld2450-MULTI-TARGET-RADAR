#include <Arduino.h>

// Optional serial-only test: uploading this REPLACES the display firmware.
// Same wiring as the original build: radar TX -> GPIO1 (ESP32 RX),
// radar RX -> GPIO2 (ESP32 TX). LD2450 UART = 256000 baud.
#define RADAR_RX 1
#define RADAR_TX 2
HardwareSerial radar(1);
uint8_t frame[30];
uint8_t pos = 0;

int16_t decodeValue(uint8_t lo, uint8_t hi) {
  const uint16_t raw = uint16_t(lo) | (uint16_t(hi) << 8);
  const int16_t magnitude = raw & 0x7FFF;
  return (raw & 0x8000) ? magnitude : -magnitude;
}

bool readOneFrame() {
  const uint8_t header[4] = {0xAA, 0xFF, 0x03, 0x00};
  while (radar.available()) {
    uint8_t b = radar.read();
    if (pos < 4) {
      if (b == header[pos]) frame[pos++] = b;
      else {
        pos = (b == header[0]) ? 1 : 0;
        if (pos) frame[0] = b;
      }
      continue;
    }
    frame[pos++] = b;
    if (pos == sizeof(frame)) {
      pos = 0;
      if (frame[28] == 0x55 && frame[29] == 0xCC) return true;
    }
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  radar.setRxBufferSize(1024);
  radar.begin(256000, SERIAL_8N1, RADAR_RX, RADAR_TX);
  Serial.println("LD2450 serial debug -- waiting for target frames...");
}

void loop() {
  if (!readOneFrame()) return;
  Serial.println("----- LD2450 RADAR -----");
  int n = 0;
  for (int i = 0; i < 3; i++) {
    const int p = 4 + 8 * i;
    bool empty = true;
    for (int j = 0; j < 8; j++) if (frame[p+j]) empty = false;
    if (empty) continue;
    const int16_t x = decodeValue(frame[p], frame[p+1]);
    const int16_t y = decodeValue(frame[p+2], frame[p+3]);
    const int16_t speed = decodeValue(frame[p+4], frame[p+5]);
    Serial.printf("Target %d | X: %d mm | Y: %d mm | Speed: %d cm/s\n",
                  i+1, x, y, speed);
    ++n;
  }
  Serial.printf("Total Targets: %d\n\n", n);
  delay(400);  // readable logging pace, not the sensor's native update rate
}
