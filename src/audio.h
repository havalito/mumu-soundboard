#pragma once
#include <Arduino.h>

// Built-in synthesized effects, used when a key has no WAV files yet
// and for little UI sounds.
enum SynthFx : uint8_t {
  FX_GIGGLE = 0,   // bouncy arpeggio (key 1 fallback)
  FX_SLIDE_UP,     // slide whistle up (key 2)
  FX_RASPBERRY,    // pfffbbt (key 3)
  FX_LOVE,         // sweet "aww" swoop (key 4)
  FX_DIZZY,        // wobbly descending wah (key 5)
  FX_BLIP,         // volume click
  FX_HELLO,        // power-on jingle
  FX_GOODNIGHT,    // falling asleep
  FX_POP,          // touch poke
  FX_BOING,        // spare
  FX_SONG_0,       // sung nursery tunes (idle singing): Twinkle Twinkle,
  FX_SONG_1,       //   Frère Jacques (Broder Jakob), Mary Had a Little Lamb,
  FX_SONG_2,       //   Ode to Joy, Old MacDonald
  FX_SONG_3,
  FX_SONG_4,
  FX_COUNT
};
#define NUM_SONGS 5

namespace Audio {
  bool begin();                        // I2S + codec + playback task
  bool playFile(const String &path);   // interrupts whatever is playing
  void playFx(SynthFx fx);
  void stop();
  bool isPlaying();
  float level();                       // 0..1, current loudness (for the mouth)
  void setVolume(int vol);             // 0..10, applies settings.maxDb cap
  void powerDown();                    // amp off, codec standby
  void powerUp();
  bool codecOk();
}
