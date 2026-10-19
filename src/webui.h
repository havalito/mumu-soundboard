#pragma once
#include <Arduino.h>
#include "face.h"

// Setup mode: Wi-Fi access point (or your home Wi-Fi) + a web page for
// uploading sounds, changing settings and updating the firmware.
namespace WebUI {
  void begin();
  void loop();
  bool active();
  String line(int i);                       // 3 status lines shown on the screen
  void onSettingsChanged(void (*cb)());
  void onPreview(void (*cb)(Reaction));    // show an animation on the device
}
