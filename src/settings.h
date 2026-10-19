#pragma once
#include <Arduino.h>

struct Settings {
  int volume;        // 0..10
  int maxDb;         // dB at volume 10 (kid-safe cap, -30..0)
  int sleepSec;      // idle seconds -> sleepy/dimmed
  int offSec;        // idle seconds -> screen off + low-power sleep (0 = never)
  int color;         // body colour preset
  int brightness;    // backlight 10..100 %
  String apPass;     // setup-mode access point password (8+ chars)
  String staSsid;    // optional home Wi-Fi for setup mode
  String staPass;
  bool shuffle;      // pick sounds in a key's folder randomly (else in order)
  int idleSec;       // sing / play an idle sound after this many idle seconds (0 = never)
  bool songs;        // include the built-in songs in idle singing
};

extern Settings settings;

void settingsLoad();
void settingsSave();
float volumeToDb(int vol);   // maps 0..10 to codec dB using maxDb
