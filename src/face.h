#pragma once
// MUMU's face: a procedurally drawn cartoon monster that blinks, looks around,
// reacts to every key, talks along with the sound, sings, gets sleepy and snores.
// Pure drawing code — no hardware access — so it can also be rendered on a PC.
// web/face.js is a line-by-line port used for the preview on the setup page;
// keep the two in step when changing an animation.
#include <LovyanGFX.hpp>
#include <stdint.h>

enum FaceMode : uint8_t { FM_AWAKE, FM_SLEEPY, FM_SLEEP, FM_CONFIG, FM_LOWBATT, FM_EMPTY };

enum Reaction : uint8_t {
  R_NONE = 0,
  // --- pickable per sound (order = REACTION_NAMES) ---
  R_LAUGH,      // ^^ eyes, huge grin, bouncing, "HA HA"
  R_SURPRISE,   // "gasp": wide eyes, tiny pupils, O mouth, "!"
  R_SILLY,      // cross-eyed, tongue out, wobble
  R_LOVE,       // heart eyes, blush, floating hearts
  R_DIZZY,      // spiral eyes, wavy mouth, orbiting stars
  R_ANGRY,      // grumpy brows, growl, steam puffs
  R_SCARED,     // tiny pupils, trembling, chattering teeth, sweat
  R_SING,       // happy closed eyes, swaying, music notes
  R_COOL,       // sunglasses, smirk, raised brow
  R_SNEEZE,     // ah... ah... ACHOO!
  R_SAD,        // droopy, wobbly frown, a tear
  R_PARTY,      // star eyes, party hat, confetti
  R_KISS,       // wink + pucker + heart
  R_TOOT,       // oops! green cloud, embarrassed grin
  R_PICK_END,
  // --- system ---
  R_MASH = R_PICK_END,  // keys mashed: extra dizzy + "WHOA"
  R_POKE,       // touched the screen
  R_HELLO,      // power on
  R_WAKE,       // woken up from sleep
  R_YAWN,       // idle
  R_WHISTLE,    // idle
  R_LOOK,       // idle: looks around
  R_GOODNIGHT,  // falling asleep
  R_COUNT
};

// Names used on the SD card (_anim.txt) and the web page
const char *reactionName(Reaction r);
Reaction reactionFromName(const char *name);   // R_NONE if unknown

class Face {
public:
  void begin(lgfx::LGFX_Sprite *canvas);
  void setColor(int preset);
  void setName(const char *name);
  static int colorCount();
  static const char *colorName(int i);

  void update(float dt, float audioLevel, bool audioPlaying);
  void render();

  void react(Reaction r, int key = -1);   // key = 0..4 makes the eyes glance at that key
  void glance(int key);                   // instant "I saw that" feedback on key down
  void poke(int x, int y);
  static int eyeAt(int x, int y);         // 0 = left eye (as you look at it), 1 = right, -1 = neither
  void setMode(FaceMode m);
  FaceMode mode() const { return _mode; }
  bool busy() const { return _react != R_NONE; }
  Reaction reaction() const { return _react; }

  void showVolume(int vol);                       // overlay for ~1.5 s
  void setBattery(int mv, bool charging, bool show);
  void setConfigText(const char *l1, const char *l2, const char *l3);

private:
  struct Params {
    float eyeOpenL, eyeOpenR, eyeScale, pupilX, pupilY, pupilSize, pupilCross;
    float browH, browTilt, browAsym, mouthOpen, mouthWidth, mouthCurve, blush;
  };
  enum EyeStyle : uint8_t { EYE_NORMAL, EYE_HAPPY, EYE_CLOSED, EYE_HEART, EYE_SPIRAL, EYE_STAR, EYE_SHADES };
  enum MouthStyle : uint8_t { M_NORMAL, M_O, M_WAVY, M_TONGUE, M_KISS };
  enum PType : uint8_t { P_HEART, P_STAR, P_NOTE, P_Z, P_TEXT, P_SPARKLE, P_SWEAT, P_ORBIT,
                         P_PUFF, P_CONFETTI, P_TEAR, P_DROP, P_BUBBLE };
  struct Particle {
    bool alive; PType type; float x, y, vx, vy, life, maxLife, size, angle; uint16_t color; const char *text;
  };

  void idleBehaviour(float dt);
  void applyReaction(float dt);
  void modeTargets();
  void spawn(PType t, float x, float y, float vx, float vy, float life, float size,
             uint16_t color, const char *text = nullptr);
  void updateParticles(float dt);

  void drawBackground();
  void drawEye(int idx, float ex, float ey);
  void drawShades(float ox, float oy);
  void drawBrow(int idx, float ex, float ey);
  void drawMouth(float mx, float my);
  void drawHat(float ox, float oy);
  void drawParticles();
  void drawOverlays();
  void drawHeart(float x, float y, float s, uint16_t c);
  void drawStar(float x, float y, float r, float ang, uint16_t c);
  void drawNote(float x, float y, float s, uint16_t c);
  void drawPuff(float x, float y, float s, uint16_t c);
  void arcLine(float cx, float cy, float halfW, float bulge, float thick, uint16_t c);
  uint16_t rainbow(float h);

  float frand(float a, float b);

  lgfx::LGFX_Sprite *_c = nullptr;
  Params _cur{}, _tgt{};
  EyeStyle _eyeStyle[2] = {EYE_NORMAL, EYE_NORMAL};
  MouthStyle _mouthStyle = M_NORMAL;
  FaceMode _mode = FM_AWAKE;

  Reaction _react = R_NONE;
  float _rt = 0, _rdur = 0;
  int _rkey = -1;
  bool _audio = false;
  float _audioLvl = 0, _talk = 0;

  float _time = 0;
  float _blinkT = 0, _nextBlink = 2, _blinkDur = 0.16f;
  bool _doubleBlink = false;
  float _nextSaccade = 1, _nextIdle = 12;
  float _bounce = 0, _shake = 0, _sway = 0, _tremble = 0, _lift = 0, _spiralRot = 0;
  float _zTimer = 0, _sparkTimer = 0;
  float _hat = 0;
  int _glanceKey = -1; float _glanceT = 0;
  float _pokeEye = -1;
  char _name[16] = "MUMU";
  char _hello[32] = "Hi! I'm MUMU";

  int _volShow = -1; float _volT = 0;
  int _battMv = 4000; bool _charging = false, _battShow = false;
  char _cfg[3][48] = {{0}};

  uint16_t _body, _bodyDark, _ink, _mouthDark, _tongue, _blushC, _white;
  static const int MAXP = 40;
  Particle _p[MAXP] = {};
  uint32_t _rng = 0xC0FFEE;
};
