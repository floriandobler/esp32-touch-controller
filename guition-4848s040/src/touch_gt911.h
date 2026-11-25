// Minimaler GT911-Treiber für das Guition 4848S040 (ohne INT/RST-Pin).
#pragma once
#include <Arduino.h>
#include <Wire.h>

namespace gt911 {

constexpr int PIN_SDA = 19;
constexpr int PIN_SCL = 45;

static uint8_t addr = 0;

static bool readReg(uint16_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void writeReg(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(val);
  Wire.endTransmission();
}

// Sucht den Controller auf 0x5D bzw. 0x14. Gibt true zurück, wenn gefunden.
static bool begin() {
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  for (uint8_t a : {0x5D, 0x14}) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      addr = a;
      uint8_t id[4] = {0};
      readReg(0x8140, id, 4);  // Produkt-ID, z. B. "911"
      Serial.printf("GT911 gefunden auf 0x%02X, ID: %c%c%c%c\n", a, id[0], id[1], id[2], id[3]);
      return true;
    }
  }
  Serial.println("GT911 nicht gefunden!");
  return false;
}

// Liest den ersten Touchpunkt.
// Rückgabe: -1 = keine neuen Daten, 0 = nicht berührt, 1 = berührt (x/y gesetzt)
static int read(int16_t &x, int16_t &y) {
  if (!addr) return -1;
  uint8_t status;
  if (!readReg(0x814E, &status, 1)) return -1;
  if (!(status & 0x80)) return -1;  // Puffer noch nicht bereit

  int result = 0;
  if ((status & 0x0F) > 0) {
    uint8_t p[4];
    if (readReg(0x8150, p, 4)) {
      x = p[0] | (p[1] << 8);
      y = p[2] | (p[3] << 8);
      result = 1;
    }
  }
  writeReg(0x814E, 0);  // Status quittieren
  return result;
}

}  // namespace gt911
