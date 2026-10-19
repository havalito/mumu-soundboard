#include "es8311.h"

// Register sequence follows Espressif's es8311 driver (esp-adf / esp_codec_dev).

bool ES8311::write(uint8_t reg, uint8_t val) {
  _w->beginTransmission(_addr);
  _w->write(reg);
  _w->write(val);
  return _w->endTransmission() == 0;
}

uint8_t ES8311::read(uint8_t reg) {
  _w->beginTransmission(_addr);
  _w->write(reg);
  if (_w->endTransmission(false) != 0) return 0;
  if (_w->requestFrom((int)_addr, 1) != 1) return 0;
  return _w->read();
}

bool ES8311::begin(TwoWire &wire, uint8_t addr) {
  _w = &wire;
  _addr = addr;
  _w->beginTransmission(_addr);
  if (_w->endTransmission() != 0) { _ok = false; return false; }

  write(0x45, 0x00);
  write(0x01, 0x30);
  write(0x02, 0x00);
  write(0x03, 0x10);
  write(0x16, 0x24);
  write(0x04, 0x10);
  write(0x05, 0x00);
  write(0x0B, 0x00);
  write(0x0C, 0x00);
  write(0x10, 0x1F);
  write(0x11, 0x7F);
  write(0x00, 0x80);                 // reset off, power-on
  write(0x00, read(0x00) & 0xBF);    // slave mode
  write(0x01, 0x3F);                 // all clocks on, MCLK from MCLK pin
  configClocks();
  write(0x13, 0x10);
  write(0x1B, 0x0A);
  write(0x1C, 0x6A);

  // Serial data port: I2S standard, 16-bit, both directions
  write(0x09, 0x0C);
  write(0x0A, 0x0C);

  _ok = true;
  start();
  setVolumeDb(-20);
  return true;
}

// MCLK = 256*fs  ->  pre_div 1, mult x1, adc/dac div 1, single speed,
// LRCK div 256, BCLK div 4, OSR 0x10. Same for every sample rate.
void ES8311::configClocks() {
  write(0x02, (read(0x02) & 0x07) | (0 << 5) | (0 << 3));
  write(0x05, 0x00);
  write(0x03, (read(0x03) & 0x80) | (0 << 6) | 0x10);
  write(0x04, (read(0x04) & 0x80) | 0x10);
  write(0x07, (read(0x07) & 0xC0) | 0x00);
  write(0x08, 0xFF);
  write(0x06, (read(0x06) & 0xE0) | (4 - 1));
  write(0x01, read(0x01) & ~0x40);   // MCLK not inverted
  write(0x06, read(0x06) & ~0x20);   // SCLK not inverted
}

void ES8311::start() {
  if (!_ok) return;
  write(0x01, 0x3F);
  write(0x00, 0x80);
  configClocks();
  write(0x09, (read(0x09) & 0xBF));          // DAC serial input on
  write(0x0A, (read(0x0A) & 0xBF) | 0x40);   // ADC serial output muted
  write(0x17, 0xBF);
  write(0x0E, 0x02);
  write(0x12, 0x00);
  write(0x14, 0x1A);
  write(0x14, read(0x14) & ~0x40);           // analog mic, not PDM
  write(0x0D, 0x01);
  write(0x15, 0x40);
  write(0x37, 0x08);
  write(0x45, 0x00);
}

void ES8311::standby() {
  if (!_ok) return;
  write(0x32, 0x00);
  write(0x17, 0x00);
  write(0x0E, 0xFF);
  write(0x12, 0x02);
  write(0x14, 0x00);
  write(0x0D, 0xFA);
  write(0x15, 0x00);
  write(0x02, 0x10);
  write(0x00, 0x00);
  write(0x00, 0x1F);
  write(0x01, 0x30);
  write(0x01, 0x00);
  write(0x45, 0x01);
}

void ES8311::setVolumeDb(float db) {
  if (!_ok) return;
  if (db < -95.5f) db = -95.5f;
  if (db > 32.0f) db = 32.0f;
  int reg = (int)lroundf(191.0f + db * 2.0f);   // 0xBF = 0 dB
  if (reg < 0) reg = 0;
  if (reg > 255) reg = 255;
  write(0x32, (uint8_t)reg);
}

void ES8311::mute(bool m) {
  if (!_ok) return;
  uint8_t v = read(0x31) & 0x9F;
  if (m) v |= 0x60;
  write(0x31, v);
}
