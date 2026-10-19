#pragma once
#include <Arduino.h>

enum InputType : uint8_t { IN_KEY_DOWN, IN_KEY_UP, IN_TOUCH, IN_TOUCH_MOVE, IN_TOUCH_UP };
// IN_TOUCH = finger down, IN_TOUCH_MOVE = still down (x,y = current), IN_TOUCH_UP = lifted (x,y = last point)

struct InputEvent {
  InputType type;
  uint8_t key;     // 0..4 for key events
  int16_t x, y;    // touch position (screen coords)
};

namespace Input {
  void begin();                        // starts a 5 ms polling task
  bool poll(InputEvent &e);            // non-blocking
  bool held(int key);
  uint32_t heldMs(int key);            // 0 if not held
  bool anyHeld();
  bool touchPresent();
  void enableWakeup();                 // configure keys as light-sleep wake sources
}
