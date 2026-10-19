#pragma once
#include <Arduino.h>
#include <vector>
#include "config.h"
#include "face.h"

// Every key has four "gestures", each with its own sound folder:
//   /sounds/1/*.wav          key 1, single press
//   /sounds/1/double/*.wav   key 1, double press
//   /sounds/1/triple/*.wav   key 1, triple press
//   /sounds/1/hold/*.wav     key 1, press and hold
//   ... same for keys 2-5
//   /sounds/idle/*.wav       played now and then when nobody is pressing anything
//   /sounds/eye-left/*.wav   poking MUMU's left eye on the touch screen
//   /sounds/eye-right/*.wav  poking the right eye
//   /sounds/hello.wav, goodnight.wav, dizzy.wav   optional specials
// Each folder can have an _anim.txt with lines "file.wav=love" choosing the
// animation for that sound (missing = the slot's default animation).
enum Gesture : uint8_t { G_PRESS = 0, G_DOUBLE, G_TRIPLE, G_HOLD, G_COUNT };

#define SLOT_IDLE  (NUM_KEYS * G_COUNT)
#define SLOT_EYE_L (SLOT_IDLE + 1)
#define SLOT_EYE_R (SLOT_IDLE + 2)
#define NUM_SLOTS  (SLOT_IDLE + 3)

namespace Sounds {
  struct Entry { String name; Reaction anim; uint32_t size; };

  bool begin();                       // mount SD, create folders
  bool mounted();
  void scan();                        // rebuild every list
  inline int slot(int key, Gesture g) { return key * G_COUNT + g; }
  int count(int slot);
  bool next(int slot, String &path, Reaction &anim);   // false if the slot is empty
  const std::vector<Entry>& list(int slot);
  String folder(int slot);
  String slotId(int slot);            // "1", "1d", "1t", "1h", ..., "idle", "eyeL", "eyeR"
  int slotFromId(const String &id);   // -1 if invalid
  Reaction defaultReaction(int slot);
  bool setAnim(int slot, const String &name, Reaction r);   // R_NONE = back to default
  String special(const char *name);   // "/sounds/<name>.wav" if it exists, else ""
  uint64_t freeBytes();
  uint64_t totalBytes();
}
