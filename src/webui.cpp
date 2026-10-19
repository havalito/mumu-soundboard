#include "webui.h"
#include "config.h"
#include "settings.h"
#include "sounds.h"
#include "audio.h"
#include "input.h"
#include "webui_html.h"     // generated from web/ by tools/build_web.py
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <FS.h>
#include <SD_MMC.h>

namespace WebUI {

static WebServer server(80);
static DNSServer dns;
static bool _active = false;
static bool _ap = true;
static String _lines[3];
static void (*_cb)() = nullptr;
static void (*_preview)(Reaction) = nullptr;
static File upFile;
static String upTmp, upFinal;
static int upSlot = -1;
static bool upOk = false;
static String upErr;
static uint32_t rebootAt = 0;

static const char *SPECIALS[] = {"hello", "goodnight", "dizzy"};

void onSettingsChanged(void (*cb)()) { _cb = cb; }
void onPreview(void (*cb)(Reaction)) { _preview = cb; }
bool active() { return _active; }
String line(int i) { return (i >= 0 && i < 3) ? _lines[i] : String(); }

// ------------------------------------------------------------- helpers ----
static String jsonEsc(const String &s) {
  String o;
  o.reserve(s.length() + 4);
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

static String cleanName(String n) {
  int slash = max(n.lastIndexOf('/'), n.lastIndexOf('\\'));
  if (slash >= 0) n = n.substring(slash + 1);
  String o;
  for (char c : n) {
    if (isalnum((uint8_t)c) || c == '-' || c == '_' || c == '.' || c == ' ') o += c;
  }
  o.trim();
  while (o.startsWith(".") || o.startsWith("_")) o.remove(0, 1);
  String l = o; l.toLowerCase();
  if (!l.endsWith(".wav")) o += ".wav";
  if (o.length() > 44) o = o.substring(0, 40) + ".wav";
  if (o == ".wav") o = "sound.wav";
  return o;
}

static bool isSpecial(const String &id) {
  for (auto s : SPECIALS) if (id == s) return true;
  return false;
}

// slot id ("1", "3d", "idle", ...) + file name -> path; specials -> /sounds/<x>.wav
static String slotPath(const String &id, const String &name) {
  if (isSpecial(id)) return String("/sounds/") + id + ".wav";
  int s = Sounds::slotFromId(id);
  if (s < 0) return "";
  return Sounds::folder(s) + "/" + cleanName(name);
}

static void sendJson(const String &j) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// ----------------------------------------------------------- handlers -----
static void hState() {
  String j;
  j.reserve(4096);
  j = "{\"name\":\"" MONSTER_NAME "\",\"fw\":\"" FW_VERSION "\",";
  j += "\"batt\":" + String(analogReadMilliVolts(BAT_ADC) * 2) + ",";
  j += "\"usb\":" + String(digitalRead(USB_SENSE) ? "true" : "false") + ",";
  j += "\"touch\":" + String(Input::touchPresent() ? "true" : "false") + ",";
  j += "\"sd\":" + String(Sounds::mounted() ? "true" : "false") + ",";
  j += "\"sdTotal\":" + String((double)Sounds::totalBytes(), 0) + ",";
  j += "\"sdFree\":" + String((double)Sounds::freeBytes(), 0) + ",";
  j += "\"slots\":{";
  for (int s = 0; s < NUM_SLOTS; s++) {
    if (s) j += ",";
    j += "\"" + Sounds::slotId(s) + "\":{\"def\":\"" + reactionName(Sounds::defaultReaction(s)) + "\",\"files\":[";
    const auto &L = Sounds::list(s);
    for (size_t i = 0; i < L.size(); i++) {
      if (i) j += ",";
      j += "{\"n\":\"" + jsonEsc(L[i].name) + "\",\"s\":" + String(L[i].size) +
           ",\"a\":\"" + reactionName(L[i].anim) + "\"}";
    }
    j += "]}";
  }
  j += "},\"special\":{";
  for (int i = 0; i < 3; i++) {
    if (i) j += ",";
    j += String("\"") + SPECIALS[i] + "\":" + (Sounds::special(SPECIALS[i]).length() ? "true" : "false");
  }
  j += "},\"anims\":[";
  for (int r = R_LAUGH; r < R_PICK_END; r++) { if (r > R_LAUGH) j += ","; j += String("\"") + reactionName((Reaction)r) + "\""; }
  j += "],\"set\":{";
  j += "\"volume\":" + String(settings.volume);
  j += ",\"maxDb\":" + String(settings.maxDb);
  j += ",\"sleep\":" + String(settings.sleepSec);
  j += ",\"off\":" + String(settings.offSec);
  j += ",\"idle\":" + String(settings.idleSec);
  j += ",\"songs\":" + String(settings.songs ? "true" : "false");
  j += ",\"color\":" + String(settings.color);
  j += ",\"bright\":" + String(settings.brightness);
  j += ",\"shuffle\":" + String(settings.shuffle ? "true" : "false");
  j += ",\"apPass\":\"" + jsonEsc(settings.apPass) + "\"";
  j += ",\"ssid\":\"" + jsonEsc(settings.staSsid) + "\"";
  j += "},\"colors\":[";
  for (int i = 0; i < Face::colorCount(); i++) { if (i) j += ","; j += String("\"") + Face::colorName(i) + "\""; }
  j += "]}";
  sendJson(j);
}

static void hSettings() {
  if (server.hasArg("volume"))  settings.volume = constrain(server.arg("volume").toInt(), 0, 10);
  if (server.hasArg("maxDb"))   settings.maxDb = constrain(server.arg("maxDb").toInt(), -30, 0);
  if (server.hasArg("sleep"))   settings.sleepSec = max(10L, server.arg("sleep").toInt());
  if (server.hasArg("off"))     settings.offSec = max(0L, server.arg("off").toInt());
  if (server.hasArg("idle"))    settings.idleSec = max(0L, server.arg("idle").toInt());
  if (server.hasArg("songs"))   settings.songs = server.arg("songs") == "1";
  if (server.hasArg("color"))   settings.color = constrain(server.arg("color").toInt(), 0, Face::colorCount() - 1);
  if (server.hasArg("bright"))  settings.brightness = constrain(server.arg("bright").toInt(), 10, 100);
  if (server.hasArg("shuffle")) settings.shuffle = server.arg("shuffle") == "1";
  if (server.hasArg("apPass") && server.arg("apPass").length() >= 8) settings.apPass = server.arg("apPass");
  if (server.hasArg("ssid"))    settings.staSsid = server.arg("ssid");
  if (server.hasArg("spass") && server.arg("spass").length()) settings.staPass = server.arg("spass");
  if (server.hasArg("ssid") && server.arg("ssid").isEmpty()) settings.staPass = "";
  settingsSave();
  if (_cb) _cb();
  sendJson("{\"ok\":true}");
}

static void hUploadData() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    upOk = false;
    upErr = "";
    String id = server.arg("slot");
    upSlot = Sounds::slotFromId(id);
    upFinal = slotPath(id, server.hasArg("name") ? server.arg("name") : u.filename);
    if (!Sounds::mounted()) { upErr = "No SD card"; return; }
    if (upFinal.isEmpty()) { upErr = "Bad slot"; return; }
    upTmp = "/sounds/upload.tmp";
    if (SD_MMC.exists(upTmp)) SD_MMC.remove(upTmp);
    upFile = SD_MMC.open(upTmp, FILE_WRITE);
    if (!upFile) upErr = "Cannot write to SD";
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upFile && upFile.write(u.buf, u.currentSize) != u.currentSize) { upErr = "SD write failed (card full?)"; upFile.close(); }
  } else if (u.status == UPLOAD_FILE_END) {
    if (upFile) {
      upFile.close();
      if (upErr.isEmpty()) {
        if (SD_MMC.exists(upFinal)) SD_MMC.remove(upFinal);
        upOk = SD_MMC.rename(upTmp, upFinal);
        if (!upOk) upErr = "Rename failed";
      }
    }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (upFile) upFile.close();
    upErr = "Upload aborted";
  }
}

static void hUploadDone() {
  Sounds::scan();
  if (upOk && upSlot >= 0 && server.hasArg("anim")) {
    String name = upFinal.substring(upFinal.lastIndexOf('/') + 1);
    Sounds::setAnim(upSlot, name, reactionFromName(server.arg("anim").c_str()));
  }
  if (upOk) sendJson("{\"ok\":true}");
  else server.send(500, "application/json", "{\"ok\":false,\"err\":\"" + jsonEsc(upErr) + "\"}");
}

static void hDelete() {
  String id = server.arg("slot"), name = server.arg("name");
  String p = slotPath(id, name);
  bool ok = p.length() && SD_MMC.remove(p);
  int s = Sounds::slotFromId(id);
  Sounds::scan();
  if (s >= 0) Sounds::setAnim(s, cleanName(name), R_NONE);   // drop its _anim.txt line
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void hAnim() {
  int s = Sounds::slotFromId(server.arg("slot"));
  Reaction r = reactionFromName(server.arg("anim").c_str());
  bool ok = s >= 0 && Sounds::setAnim(s, cleanName(server.arg("name")), r);
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

// Play on the device. ?slot&name = that sound with its animation,
// ?anim=x also shows/overrides the animation, ?fx=n a built-in effect.
static void hPlay() {
  Reaction r = R_NONE;
  if (server.hasArg("anim")) r = reactionFromName(server.arg("anim").c_str());
  if (server.hasArg("fx")) {
    Audio::playFx((SynthFx)server.arg("fx").toInt());
  } else if (server.hasArg("slot")) {
    String id = server.arg("slot");
    String p = slotPath(id, server.arg("name"));
    if (p.isEmpty() || !SD_MMC.exists(p)) { sendJson("{\"ok\":false}"); return; }
    Audio::playFile(p);
    int s = Sounds::slotFromId(id);
    if (r == R_NONE && s >= 0) {
      r = Sounds::defaultReaction(s);
      String n = cleanName(server.arg("name"));
      for (auto &e : Sounds::list(s)) if (e.name == n && e.anim != R_NONE) r = e.anim;
    }
  }
  if (r != R_NONE && _preview) _preview(r);
  sendJson("{\"ok\":true}");
}

// Stream a sound to the browser for the preview
static void hFile() {
  String p = slotPath(server.arg("slot"), server.arg("name"));
  File f = p.length() ? SD_MMC.open(p) : File();
  if (!f) { server.send(404, "text/plain", "not found"); return; }
  server.sendHeader("Cache-Control", "max-age=60");
  server.streamFile(f, "audio/wav");
  f.close();
}

static void hFwData() {
  HTTPUpload &u = server.upload();
  if (u.status == UPLOAD_FILE_START) {
    Audio::stop();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (Update.write(u.buf, u.currentSize) != u.currentSize) Update.printError(Serial);
  } else if (u.status == UPLOAD_FILE_END) {
    if (!Update.end(true)) Update.printError(Serial);
  }
}

static void hFwDone() {
  bool ok = !Update.hasError();
  sendJson(ok ? "{\"ok\":true}" : "{\"ok\":false,\"err\":\"" + jsonEsc(Update.errorString()) + "\"}");
  if (ok) rebootAt = millis() + 800;
}

static void hReboot() {
  sendJson("{\"ok\":true}");
  rebootAt = millis() + 500;
}

static void hRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html", (const char *)INDEX_HTML_GZ, INDEX_HTML_GZ_LEN);
}

// Captive portal: send anything unknown to the page
static void hNotFound() {
  if (_ap) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ------------------------------------------------------------- start ------
void begin() {
  if (_active) return;
  _active = true;

  String host = "mumu";
  WiFi.persistent(false);
  bool sta = false;
  if (settings.staSsid.length()) {
    _lines[0] = "Joining " + settings.staSsid;
    _lines[1] = "please wait...";
    _lines[2] = "";
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(host.c_str());
    WiFi.begin(settings.staSsid.c_str(), settings.staPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) delay(100);
    sta = WiFi.status() == WL_CONNECTED;
  }

  if (sta) {
    _ap = false;
    MDNS.begin(host.c_str());
    MDNS.addService("http", "tcp", 80);
    _lines[0] = "Wi-Fi: " + settings.staSsid;
    _lines[1] = "http://" + host + ".local";
    _lines[2] = "http://" + WiFi.localIP().toString();
  } else {
    _ap = true;
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char ssid[24];
    snprintf(ssid, sizeof(ssid), MONSTER_NAME "-%02X%02X", mac[4], mac[5]);
    WiFi.softAP(ssid, settings.apPass.c_str());
    delay(100);
    dns.start(53, "*", WiFi.softAPIP());
    _lines[0] = String("Wi-Fi: ") + ssid;
    _lines[1] = "Pass: " + settings.apPass;
    _lines[2] = "http://" + WiFi.softAPIP().toString();
  }

  server.on("/", HTTP_GET, hRoot);
  server.on("/api/state", HTTP_GET, hState);
  server.on("/api/settings", HTTP_POST, hSettings);
  server.on("/api/upload", HTTP_POST, hUploadDone, hUploadData);
  server.on("/api/delete", HTTP_POST, hDelete);
  server.on("/api/anim", HTTP_POST, hAnim);
  server.on("/api/play", HTTP_POST, hPlay);
  server.on("/api/file", HTTP_GET, hFile);
  server.on("/api/reboot", HTTP_POST, hReboot);
  server.on("/update", HTTP_POST, hFwDone, hFwData);
  server.onNotFound(hNotFound);
  server.begin();
  log_i("setup mode: %s / %s / %s", _lines[0].c_str(), _lines[1].c_str(), _lines[2].c_str());
}

void loop() {
  if (!_active) return;
  if (_ap) dns.processNextRequest();
  server.handleClient();
  if (rebootAt && millis() > rebootAt) {
    Audio::powerDown();
    delay(100);
    ESP.restart();
  }
}

}  // namespace WebUI
