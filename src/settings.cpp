#include "settings.h"
#include "config.h"
#include <Preferences.h>

Settings settings;
static Preferences prefs;

void settingsLoad() {
  prefs.begin("monster", true);
  settings.volume     = prefs.getInt("vol", DEFAULT_VOLUME);
  settings.maxDb      = prefs.getInt("maxdb", DEFAULT_MAX_DB);
  settings.sleepSec   = prefs.getInt("sleep", DEFAULT_SLEEP_SEC);
  settings.offSec     = prefs.getInt("off", DEFAULT_OFF_SEC);
  settings.color      = prefs.getInt("color", DEFAULT_COLOR);
  settings.brightness = prefs.getInt("bright", 80);
  settings.apPass     = prefs.getString("appass", DEFAULT_AP_PASS);
  settings.staSsid    = prefs.getString("ssid", "");
  settings.staPass    = prefs.getString("spass", "");
  settings.shuffle    = prefs.getBool("shuffle", true);
  settings.idleSec    = prefs.getInt("idle", DEFAULT_IDLE_SEC);
  settings.songs      = prefs.getBool("songs", true);
  prefs.end();

  settings.volume     = constrain(settings.volume, 0, 10);
  settings.maxDb      = constrain(settings.maxDb, -30, 0);
  settings.brightness = constrain(settings.brightness, 10, 100);
  if (settings.sleepSec < 10) settings.sleepSec = 10;
  if (settings.apPass.length() < 8) settings.apPass = DEFAULT_AP_PASS;
}

void settingsSave() {
  prefs.begin("monster", false);
  prefs.putInt("vol", settings.volume);
  prefs.putInt("maxdb", settings.maxDb);
  prefs.putInt("sleep", settings.sleepSec);
  prefs.putInt("off", settings.offSec);
  prefs.putInt("color", settings.color);
  prefs.putInt("bright", settings.brightness);
  prefs.putString("appass", settings.apPass);
  prefs.putString("ssid", settings.staSsid);
  prefs.putString("spass", settings.staPass);
  prefs.putBool("shuffle", settings.shuffle);
  prefs.putInt("idle", settings.idleSec);
  prefs.putBool("songs", settings.songs);
  prefs.end();
}

// Volume 10 = maxDb, each step down is 3 dB, 0 = silent.
float volumeToDb(int vol) {
  if (vol <= 0) return -95.5f;
  return (float)settings.maxDb - 3.0f * (10 - vol);
}
