#include "sounds.h"
#include "settings.h"
#include <FS.h>
#include <SD_MMC.h>
#include <algorithm>

namespace Sounds {

static bool _mounted = false;
static std::vector<Entry> _files[NUM_SLOTS];
static int _pos[NUM_SLOTS];
static int _last[NUM_SLOTS];

static const char *G_DIR[G_COUNT] = {"", "/double", "/triple", "/hold"};
static const char G_SUFFIX[G_COUNT] = {0, 'd', 't', 'h'};
static const Reaction KEY_DEFAULT[NUM_KEYS] = {R_LAUGH, R_SURPRISE, R_SILLY, R_LOVE, R_DIZZY};

String folder(int s) {
  if (s == SLOT_IDLE) return "/sounds/idle";
  if (s == SLOT_EYE_L) return "/sounds/eye-left";
  if (s == SLOT_EYE_R) return "/sounds/eye-right";
  return String("/sounds/") + String(s / G_COUNT + 1) + G_DIR[s % G_COUNT];
}

String slotId(int s) {
  if (s == SLOT_IDLE) return "idle";
  if (s == SLOT_EYE_L) return "eyeL";
  if (s == SLOT_EYE_R) return "eyeR";
  String id = String(s / G_COUNT + 1);
  if (s % G_COUNT) id += G_SUFFIX[s % G_COUNT];
  return id;
}

int slotFromId(const String &id) {
  if (id == "idle") return SLOT_IDLE;
  if (id == "eyeL") return SLOT_EYE_L;
  if (id == "eyeR") return SLOT_EYE_R;
  if (id.length() < 1 || id.length() > 2) return -1;
  int k = id[0] - '1';
  if (k < 0 || k >= NUM_KEYS) return -1;
  if (id.length() == 1) return slot(k, G_PRESS);
  for (int g = 1; g < G_COUNT; g++) if (id[1] == G_SUFFIX[g]) return slot(k, (Gesture)g);
  return -1;
}

Reaction defaultReaction(int s) {
  if (s == SLOT_IDLE) return R_SING;
  if (s == SLOT_EYE_L || s == SLOT_EYE_R) return R_POKE;
  return KEY_DEFAULT[s / G_COUNT];
}

bool begin() {
  pinMode(SD_D3, OUTPUT);
  digitalWrite(SD_D3, HIGH);
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0);
  _mounted = SD_MMC.begin("/sdcard", true /*1-bit*/, false, SDMMC_FREQ_DEFAULT, 5);
  if (_mounted) {
    if (!SD_MMC.exists("/sounds")) SD_MMC.mkdir("/sounds");
    for (int k = 0; k < NUM_KEYS; k++) {          // key folders first, then the gesture subfolders
      String f = folder(slot(k, G_PRESS));
      if (!SD_MMC.exists(f)) SD_MMC.mkdir(f);
    }
    for (int s = 0; s < NUM_SLOTS; s++) {
      String f = folder(s);
      if (!SD_MMC.exists(f)) SD_MMC.mkdir(f);
    }
  }
  scan();
  return _mounted;
}

bool mounted() { return _mounted; }

static bool isWav(const String &n) {
  String l = n; l.toLowerCase();
  return l.endsWith(".wav") && !l.startsWith(".") && !l.startsWith("_");
}

static void loadAnims(int s) {
  File f = SD_MMC.open(folder(s) + "/_anim.txt");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.lastIndexOf('=');
    if (eq <= 0) continue;
    String name = line.substring(0, eq);
    Reaction r = reactionFromName(line.substring(eq + 1).c_str());
    for (auto &e : _files[s]) if (e.name == name) e.anim = r;
  }
  f.close();
}

void scan() {
  for (int s = 0; s < NUM_SLOTS; s++) {
    _files[s].clear();
    _pos[s] = 0;
    _last[s] = -1;
    if (!_mounted) continue;
    File dir = SD_MMC.open(folder(s));
    if (!dir || !dir.isDirectory()) continue;
    File f;
    while ((f = dir.openNextFile())) {
      if (!f.isDirectory()) {
        String n = f.name();
        int slash = n.lastIndexOf('/');
        if (slash >= 0) n = n.substring(slash + 1);
        if (isWav(n)) _files[s].push_back({n, R_NONE, (uint32_t)f.size()});
      }
      f.close();
    }
    dir.close();
    std::sort(_files[s].begin(), _files[s].end(),
              [](const Entry &a, const Entry &b) { return a.name.compareTo(b.name) < 0; });
    loadAnims(s);
  }
}

int count(int s) { return (s >= 0 && s < NUM_SLOTS) ? _files[s].size() : 0; }

const std::vector<Entry>& list(int s) { return _files[s]; }

bool next(int s, String &path, Reaction &anim) {
  int n = count(s);
  if (n == 0) return false;
  int idx;
  if (n == 1) {
    idx = 0;
  } else if (settings.shuffle) {
    do { idx = random(n); } while (idx == _last[s]);   // never the same twice in a row
  } else {
    idx = _pos[s] % n;
    _pos[s] = (idx + 1) % n;
  }
  _last[s] = idx;
  const Entry &e = _files[s][idx];
  path = folder(s) + "/" + e.name;
  anim = (e.anim != R_NONE) ? e.anim : defaultReaction(s);
  return true;
}

bool setAnim(int s, const String &name, Reaction r) {
  if (s < 0 || s >= NUM_SLOTS || !_mounted) return false;
  bool found = false;
  for (auto &e : _files[s]) if (e.name == name) { e.anim = r; found = true; }
  File f = SD_MMC.open(folder(s) + "/_anim.txt", FILE_WRITE);
  if (!f) return false;
  for (auto &e : _files[s])
    if (e.anim != R_NONE) { f.print(e.name); f.print('='); f.println(reactionName(e.anim)); }
  f.close();
  return found;
}

String special(const char *name) {
  if (!_mounted) return "";
  String p = String("/sounds/") + name + ".wav";
  return SD_MMC.exists(p) ? p : String("");
}

uint64_t freeBytes()  { return _mounted ? SD_MMC.totalBytes() - SD_MMC.usedBytes() : 0; }
uint64_t totalBytes() { return _mounted ? SD_MMC.totalBytes() : 0; }

}  // namespace Sounds
