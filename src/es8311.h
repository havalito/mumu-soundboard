#pragma once
#include <Arduino.h>
#include <Wire.h>

// Minimal ES8311 driver: DAC playback only, codec as I2S slave,
// MCLK always 256 x sample rate (so one clock setting covers every rate).
class ES8311 {
public:
  bool begin(TwoWire &wire, uint8_t addr = 0x18);
  void start();                 // power up DAC path
  void standby();               // lowest-power state (call start() to resume)
  void setVolumeDb(float db);   // -95.5 .. +32 dB, 0.5 dB steps
  void mute(bool m);
  bool ok() const { return _ok; }

private:
  bool write(uint8_t reg, uint8_t val);
  uint8_t read(uint8_t reg);
  void configClocks();
  TwoWire *_w = nullptr;
  uint8_t _addr = 0x18;
  bool _ok = false;
};
