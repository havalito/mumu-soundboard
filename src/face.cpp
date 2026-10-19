#include "face.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

static const float EYE_Y = 98, EYE_DX = 47, EYE_R = 35;
static const float MOUTH_Y = 172;
static const float PI_F = 3.14159265f;

struct Palette { const char *name; uint8_t body[3], dark[3]; };
static const Palette PALETTES[] = {
  {"Slime green", {112, 201,  86}, { 84, 160,  62}},
  {"Grape",       {150, 100, 210}, {118,  74, 176}},
  {"Tangerine",   {255, 150,  60}, {222, 116,  38}},
  {"Sky blue",    { 80, 170, 240}, { 52, 132, 204}},
  {"Bubblegum",   {255, 120, 180}, {222,  86, 148}},
  {"Sunny",       {250, 210,  60}, {214, 172,  34}},
};

static const char *REACTION_NAMES[R_COUNT] = {
  "", "laugh", "gasp", "silly", "love", "dizzy", "angry", "scared", "sing", "cool",
  "sneeze", "sad", "party", "kiss", "toot",
  "mash", "poke", "hello", "wake", "yawn", "whistle", "look", "goodnight"
};

const char *reactionName(Reaction r) { return r < R_COUNT ? REACTION_NAMES[r] : ""; }
Reaction reactionFromName(const char *n) {
  if (!n || !*n) return R_NONE;
  for (int i = 1; i < R_COUNT; i++) if (!strcmp(n, REACTION_NAMES[i])) return (Reaction)i;
  return R_NONE;
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float approach(float cur, float tgt, float speed, float dt) {
  return cur + (tgt - cur) * (1.0f - expf(-speed * dt));
}

int Face::colorCount() { return sizeof(PALETTES) / sizeof(PALETTES[0]); }
const char *Face::colorName(int i) { return PALETTES[(i % colorCount() + colorCount()) % colorCount()].name; }

float Face::frand(float a, float b) {
  _rng ^= _rng << 13; _rng ^= _rng >> 17; _rng ^= _rng << 5;
  return a + (b - a) * ((_rng & 0xFFFFFF) / 16777216.0f);
}

uint16_t Face::rainbow(float h) {
  h = fmodf(h, 1.0f) * 6;
  int i = (int)h; float f = h - i;
  float r = 0, g = 0, b = 0;
  switch (i) {
    case 0: r = 1; g = f; break;      case 1: r = 1 - f; g = 1; break;
    case 2: g = 1; b = f; break;      case 3: g = 1 - f; b = 1; break;
    case 4: r = f; b = 1; break;      default: r = 1; b = 1 - f; break;
  }
  return _c->color565(60 + r * 195, 60 + g * 195, 60 + b * 195);
}

// ======================================================================
void Face::begin(lgfx::LGFX_Sprite *canvas) {
  _c = canvas;
  _ink       = _c->color565(40, 30, 60);
  _mouthDark = _c->color565(70, 20, 40);
  _tongue    = _c->color565(255, 110, 130);
  _white     = _c->color565(255, 255, 255);
  setColor(0);
  _cur = {1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0.6f, 0};
  _tgt = _cur;
}

void Face::setName(const char *name) {
  strncpy(_name, name, sizeof(_name) - 1);
  snprintf(_hello, sizeof(_hello), "Hi! I'm %s", _name);
}

void Face::setColor(int preset) {
  const Palette &p = PALETTES[(preset % colorCount() + colorCount()) % colorCount()];
  _body     = _c->color565(p.body[0], p.body[1], p.body[2]);
  _bodyDark = _c->color565(p.dark[0], p.dark[1], p.dark[2]);
  _blushC   = (preset % colorCount() == 4) ? _c->color565(255, 80, 130) : _c->color565(255, 140, 170);
}

void Face::setMode(FaceMode m) {
  if (m == _mode) return;
  FaceMode old = _mode;
  _mode = m;
  if (m == FM_AWAKE && (old == FM_SLEEP || old == FM_SLEEPY)) react(R_WAKE);
  if (m == FM_SLEEP || m == FM_CONFIG || m == FM_EMPTY) {
    _react = R_NONE; _zTimer = 0; _hat = 0;
    for (int i = 0; i < MAXP; i++) _p[i].alive = false;
  }
}

void Face::showVolume(int vol) { _volShow = vol; _volT = 1.6f; }

void Face::setBattery(int mv, bool charging, bool show) {
  _battMv = mv; _charging = charging; _battShow = show;
}

void Face::setConfigText(const char *l1, const char *l2, const char *l3) {
  strncpy(_cfg[0], l1 ? l1 : "", 47);
  strncpy(_cfg[1], l2 ? l2 : "", 47);
  strncpy(_cfg[2], l3 ? l3 : "", 47);
}

// ---------------------------------------------------------------- triggers
void Face::glance(int key) {
  if (key < 0 || key > 4) return;
  _glanceKey = key;
  _glanceT = 0.35f;
}

void Face::react(Reaction r, int key) {
  if (r == R_NONE || r >= R_COUNT) return;
  _react = r;
  _rt = 0;
  _rkey = key;
  _sparkTimer = 0;
  _eyeStyle[0] = _eyeStyle[1] = EYE_NORMAL;
  _mouthStyle = M_NORMAL;
  _hat = 0;
  uint16_t yellow = _c->color565(255, 225, 60);
  switch (r) {
    case R_LAUGH:     _rdur = 1.4f; break;
    case R_SURPRISE:  _rdur = 1.3f; _shake = 7;
                      spawn(P_TEXT, 120, 22, 0, -10, 1.1f, 1.6f, _c->color565(230, 40, 40), "!"); break;
    case R_SILLY:     _rdur = 1.5f; break;
    case R_LOVE:      _rdur = 1.7f; break;
    case R_DIZZY:     _rdur = 1.8f;
      for (int i = 0; i < 4; i++) spawn(P_ORBIT, 0, 0, 0, 0, 1.8f, 9, yellow);
      break;
    case R_ANGRY:     _rdur = 1.6f; _shake = 5; break;
    case R_SCARED:    _rdur = 1.8f; break;
    case R_SING:      _rdur = 2.4f; break;
    case R_COOL:      _rdur = 1.8f;
      spawn(P_SPARKLE, 150, 80, 0, 0, 0.6f, 12, _white); break;
    case R_SNEEZE:    _rdur = 2.0f; break;
    case R_SAD:       _rdur = 2.2f; break;
    case R_PARTY:     _rdur = 2.0f; _hat = 1; _bounce = 8; break;
    case R_KISS:      _rdur = 1.5f; break;
    case R_TOOT:      _rdur = 2.0f; break;
    case R_MASH:      _rdur = 2.2f; _shake = 9;
      for (int i = 0; i < 5; i++) spawn(P_ORBIT, 0, 0, 0, 0, 2.2f, 10, yellow);
      spawn(P_TEXT, 120, 222, 0, -6, 1.8f, 1.0f, _white, "WHOA!"); break;
    case R_POKE:      _rdur = 1.0f; _shake = 4; break;
    case R_HELLO:     _rdur = 2.2f;
      spawn(P_BUBBLE, 120, 26, 0, 0, 2.0f, 1, _white, _hello); break;
    case R_WAKE:      _rdur = 0.9f; _shake = 5; break;
    case R_YAWN:      _rdur = 2.4f; break;
    case R_WHISTLE:   _rdur = 2.2f; break;
    case R_LOOK:      _rdur = 2.0f; break;
    case R_GOODNIGHT: _rdur = 2.2f; break;
    default:          _rdur = 1.0f; break;
  }
  // Orbiting stars: drop old ones, spread the new ones evenly around the head
  int n = 0;
  for (int i = 0; i < MAXP; i++)
    if (_p[i].alive && _p[i].type == P_ORBIT) {
      if (_p[i].life < _p[i].maxLife) _p[i].alive = false; else n++;
    }
  int k = 0;
  for (int i = 0; i < MAXP; i++)
    if (_p[i].alive && _p[i].type == P_ORBIT) _p[i].angle = (k++) * 2 * PI_F / n;
}

int Face::eyeAt(int x, int y) {
  for (int i = 0; i < 2; i++) {
    float ex = 120 + (i ? EYE_DX : -EYE_DX);
    if ((x - ex) * (x - ex) + (y - EYE_Y) * (y - EYE_Y) < (EYE_R + 14) * (EYE_R + 14)) return i;
  }
  return -1;
}

void Face::poke(int x, int y) {
  if (_mode == FM_SLEEP || _mode == FM_SLEEPY) { setMode(FM_AWAKE); return; }
  _pokeEye = eyeAt(x, y);
  react(R_POKE);
}

// ---------------------------------------------------------------- particles
void Face::spawn(PType t, float x, float y, float vx, float vy, float life, float size,
                 uint16_t color, const char *text) {
  for (int i = 0; i < MAXP; i++) {
    if (!_p[i].alive) {
      _p[i] = {true, t, x, y, vx, vy, life, life, size, 0, color, text};
      return;
    }
  }
}

void Face::updateParticles(float dt) {
  for (int i = 0; i < MAXP; i++) {
    Particle &p = _p[i];
    if (!p.alive) continue;
    p.life -= dt;
    if (p.life <= 0) { p.alive = false; continue; }
    if (p.type == P_ORBIT) { p.angle += dt * 5.0f; continue; }
    p.x += p.vx * dt;
    p.y += p.vy * dt;
    if (p.type == P_HEART || p.type == P_NOTE || p.type == P_Z) p.x += sinf((_time + i) * 4) * 12 * dt;
    if (p.type == P_SWEAT || p.type == P_TEAR || p.type == P_DROP) p.vy += 160 * dt;
    if (p.type == P_CONFETTI) { p.vy += 60 * dt; p.x += sinf((_time + i) * 6) * 30 * dt; }
    if (p.type == P_PUFF) { p.size += 14 * dt; p.vx *= 0.97f; p.vy *= 0.97f; }
    p.angle += dt * 3;
  }
}

// ------------------------------------------------------------ behaviour ---
void Face::modeTargets() {
  Params &t = _tgt;
  t = {1, 1, 1, t.pupilX, t.pupilY, 1, 0, 0, 0, 0, 0, 1, 0.55f, 0};
  switch (_mode) {
    case FM_SLEEPY:  t.eyeOpenL = t.eyeOpenR = 0.38f; t.browH = -0.3f; t.mouthCurve = 0.1f; t.pupilY = 0.3f; break;
    case FM_SLEEP:   t.mouthOpen = 0.12f + 0.06f * sinf(_time * 1.6f); t.mouthWidth = 0.35f; t.browH = -0.4f; break;
    case FM_CONFIG:  t.pupilY = 0.8f; t.pupilX = 0.3f * sinf(_time * 0.7f); t.mouthCurve = 0.7f; break;
    case FM_LOWBATT: t.eyeOpenL = t.eyeOpenR = 0.6f; t.browTilt = 0.8f; t.mouthCurve = -0.3f; break;
    case FM_EMPTY:   t.eyeOpenL = t.eyeOpenR = 0.5f; t.browTilt = 1.0f; t.mouthCurve = -0.8f; t.pupilY = 0.5f; break;
    default: break;
  }
  _eyeStyle[0] = _eyeStyle[1] = (_mode == FM_SLEEP) ? EYE_CLOSED : EYE_NORMAL;
  _mouthStyle = (_mode == FM_SLEEP) ? M_O : M_NORMAL;
}

void Face::idleBehaviour(float dt) {
  // Blinking
  _blinkT -= dt;
  _nextBlink -= dt;
  if (_nextBlink <= 0 && _mode != FM_SLEEP) {
    _blinkDur = (_mode == FM_SLEEPY) ? 0.45f : 0.16f;
    _blinkT = _blinkDur;
    if (_doubleBlink) { _nextBlink = 0.22f; _doubleBlink = false; }
    else { _nextBlink = (_mode == FM_SLEEPY) ? frand(1.5f, 3.5f) : frand(1.8f, 5.5f); _doubleBlink = frand(0, 1) < 0.18f; }
  }

  // Looking around (pupil saccades)
  _nextSaccade -= dt;
  if (_nextSaccade <= 0 && _react == R_NONE && (_mode == FM_AWAKE || _mode == FM_LOWBATT)) {
    _nextSaccade = frand(0.7f, 2.6f);
    if (frand(0, 1) < 0.35f) { _tgt.pupilX = 0; _tgt.pupilY = 0; }
    else { _tgt.pupilX = frand(-0.9f, 0.9f); _tgt.pupilY = frand(-0.6f, 0.7f); }
  }

  // Occasional silent idle animation
  if (_mode == FM_AWAKE && _react == R_NONE && !_audio) {
    _nextIdle -= dt;
    if (_nextIdle <= 0) {
      _nextIdle = frand(9, 22);
      float r = frand(0, 1);
      react(r < 0.3f ? R_YAWN : (r < 0.65f ? R_WHISTLE : R_LOOK));
    }
  }

  // Snoring Zzz
  if (_mode == FM_SLEEP) {
    _zTimer -= dt;
    if (_zTimer <= 0) {
      _zTimer = 1.1f;
      spawn(P_Z, 206, 168, 5, -34, 2.6f, frand(0.8f, 1.2f), _white, "z");
    }
  }
  if (_mode == FM_LOWBATT && frand(0, 1) < dt * 0.4f) spawn(P_SWEAT, 186, 52, 0, 0, 1.0f, 7, _c->color565(120, 200, 255));
}

void Face::applyReaction(float dt) {
  if (_react == R_NONE) return;
  _rt += dt;
  float t = _rt, u = clampf(t / _rdur, 0, 1);
  Params &g = _tgt;

  // Glance toward the key that was pressed (keys sit in an arc below the screen)
  if (_rkey >= 0 && t < 0.35f) { g.pupilX = (_rkey - 2) * 0.45f; g.pupilY = 0.8f; }

  switch (_react) {
    case R_LAUGH:
      _eyeStyle[0] = _eyeStyle[1] = EYE_HAPPY;
      g.mouthOpen = 0.75f + 0.25f * fabsf(sinf(t * 14)); g.mouthCurve = 1; g.mouthWidth = 1.15f;
      g.browH = 0.6f; g.blush = 0.6f; _bounce = 9;
      _sparkTimer -= dt;
      if (_sparkTimer <= 0 && t < 1.1f) {
        _sparkTimer = 0.35f;
        bool left = ((int)(t * 3)) % 2;
        spawn(P_TEXT, left ? 44 : 196, 180, left ? -18 : 18, -60, 1.0f, 0.75f, _white, "HA");
      }
      break;
    case R_SURPRISE:
      g.eyeScale = 1.25f; g.pupilSize = 0.45f; g.browH = 1; g.browTilt = 0.2f;
      _mouthStyle = M_O; g.mouthOpen = 0.8f; g.mouthWidth = 0.55f;
      if (t > 0.35f) { g.pupilX = 0; g.pupilY = 0; }
      break;
    case R_SILLY:
      g.pupilCross = 1.0f; g.pupilX = 0; g.pupilY = 0.15f; g.pupilSize = 0.75f;
      _mouthStyle = M_TONGUE; g.mouthCurve = 0.8f; g.browH = 0.5f; g.browTilt = -0.4f + 0.8f * sinf(t * 10);
      _sway = 6 * sinf(t * 9);
      break;
    case R_LOVE:
      _eyeStyle[0] = _eyeStyle[1] = EYE_HEART;
      g.blush = 1; g.mouthCurve = 1; g.mouthOpen = 0.25f; g.mouthWidth = 0.8f; g.browH = 0.4f; g.browTilt = 0.5f;
      _sparkTimer -= dt;
      if (_sparkTimer <= 0 && t < 1.3f) {
        _sparkTimer = 0.18f;
        spawn(P_HEART, frand(30, 210), 230, frand(-15, 15), frand(-90, -60), 1.6f, frand(7, 13), _c->color565(255, 60, 110));
      }
      break;
    case R_DIZZY:
    case R_MASH:
      _eyeStyle[0] = _eyeStyle[1] = EYE_SPIRAL;
      _spiralRot += dt * (_react == R_MASH ? 14 : 9);
      _mouthStyle = M_WAVY; g.mouthWidth = 0.8f; g.browTilt = 0.6f * sinf(t * 6);
      _sway = (_react == R_MASH ? 12 : 8) * sinf(t * 5);
      break;
    case R_ANGRY:
      g.browTilt = -1.2f; g.browH = -0.5f; g.eyeOpenL = g.eyeOpenR = 0.72f; g.pupilSize = 0.8f;
      g.mouthCurve = -0.9f; g.mouthOpen = 0.32f + 0.08f * sinf(t * 20); g.mouthWidth = 0.9f;
      if (t > 0.35f) { g.pupilX = 0; g.pupilY = 0.1f; }
      if (fmodf(t, 0.6f) < dt) _shake = 4;
      _sparkTimer -= dt;
      if (_sparkTimer <= 0 && t < 1.4f) {
        _sparkTimer = 0.22f;
        bool left = ((int)(t * 5)) % 2;
        spawn(P_PUFF, left ? 22 : 218, 70, left ? -10 : 10, -55, 0.9f, 7, _c->color565(240, 240, 250));
      }
      break;
    case R_SCARED:
      g.eyeScale = 1.15f; g.pupilSize = 0.38f; g.browH = 0.8f; g.browTilt = 1.1f;
      _mouthStyle = M_WAVY; g.mouthWidth = 0.55f;
      g.pupilX = 0.25f * sinf(t * 17); g.pupilY = -0.1f;
      _tremble = 2.2f;
      _sparkTimer -= dt;
      if (_sparkTimer <= 0 && t < 1.5f) {
        _sparkTimer = 0.4f;
        spawn(P_SWEAT, frand(0, 1) < 0.5f ? 36 : 204, 56, 0, 0, 1.0f, 6, _c->color565(120, 200, 255));
      }
      break;
    case R_SING:
      _eyeStyle[0] = _eyeStyle[1] = EYE_HAPPY;
      _mouthStyle = M_O; g.mouthOpen = 0.3f; g.mouthWidth = 0.5f; g.browH = 0.7f; g.blush = 0.4f;
      _sway = 7 * sinf(t * 4.2f);
      _sparkTimer -= dt;
      if (_sparkTimer <= 0) {
        _sparkTimer = 0.42f;
        bool left = ((int)(t * 2.4f)) % 2;
        spawn(P_NOTE, left ? 60 : 180, 200, left ? -25 : 25, frand(-60, -45), 1.6f, frand(8, 11),
              rainbow(frand(0, 1)));
      }
      break;
    case R_COOL:
      _eyeStyle[0] = _eyeStyle[1] = EYE_SHADES;
      g.mouthCurve = 0.6f; g.mouthWidth = 0.75f; g.browAsym = 0.9f; g.browH = 0.2f;
      _sway = 3 * sinf(t * 3);
      break;
    case R_SNEEZE:
      if (t < 0.95f) {                 // ah... ah...
        float k = t / 0.95f;
        g.eyeOpenL = g.eyeOpenR = 1 - 0.75f * k; g.browH = 0.9f * k; g.browTilt = 0.6f * k;
        _mouthStyle = M_O; g.mouthOpen = 0.2f + 0.6f * k; g.mouthWidth = 0.45f;
        _lift = -10 * k;
      } else if (t < 1.45f) {          // CHOO!
        if (t - dt < 0.95f) {
          _shake = 10;
          spawn(P_TEXT, 120, 30, 0, -8, 1.1f, 1.05f, _white, "ACHOO!");
          for (int i = 0; i < 9; i++)
            spawn(P_DROP, 120 + frand(-30, 30), 185, frand(-140, 140), frand(-60, 40), 0.7f, frand(2.5f, 4.5f),
                  _c->color565(150, 215, 255));
        }
        _eyeStyle[0] = _eyeStyle[1] = EYE_CLOSED;
        g.mouthOpen = 0.9f; g.mouthWidth = 1.1f; g.mouthCurve = 0.2f; g.browH = -0.3f;
        _lift = 8;
      } else {                         // phew
        g.mouthCurve = 0.6f; g.blush = 0.5f; _lift = 0;
      }
      break;
    case R_SAD:
      g.browTilt = 1.1f; g.browH = 0.1f; g.eyeOpenL = g.eyeOpenR = 0.8f; g.pupilY = 0.55f; g.pupilSize = 1.15f;
      g.mouthCurve = -0.85f + 0.08f * sinf(t * 14); g.mouthWidth = 0.7f;
      if (t > 0.35f) g.pupilX = 0;
      if (t - dt < 0.5f && t >= 0.5f) spawn(P_TEAR, 120 - EYE_DX + 16, EYE_Y + 30, 0, 10, 1.4f, 6, _c->color565(120, 200, 255));
      if (t - dt < 1.2f && t >= 1.2f) spawn(P_TEAR, 120 + EYE_DX - 16, EYE_Y + 30, 0, 10, 1.2f, 5, _c->color565(120, 200, 255));
      break;
    case R_PARTY:
      _eyeStyle[0] = _eyeStyle[1] = EYE_STAR;
      g.mouthCurve = 1; g.mouthOpen = 0.65f + 0.2f * fabsf(sinf(t * 10)); g.mouthWidth = 1.1f; g.blush = 0.5f;
      _bounce = 7; _hat = 1;
      _sparkTimer -= dt;
      if (_sparkTimer <= 0 && t < 1.6f) {
        _sparkTimer = 0.05f;
        spawn(P_CONFETTI, frand(0, 240), -6, frand(-20, 20), frand(40, 90), 2.2f, frand(4, 7), rainbow(frand(0, 1)));
      }
      break;
    case R_KISS:
      _eyeStyle[0] = EYE_CLOSED;
      _mouthStyle = M_KISS; g.blush = 0.9f; g.browAsym = 0.5f; g.pupilX = 0.4f; g.pupilY = -0.2f;
      if (t - dt < 0.35f && t >= 0.35f) spawn(P_HEART, 140, 168, 30, -50, 1.3f, 11, _c->color565(255, 60, 110));
      break;
    case R_TOOT:
      if (t < 0.45f) {
        g.eyeScale = 1.2f; g.pupilSize = 0.5f; g.browH = 1; _mouthStyle = M_O; g.mouthOpen = 0.4f; g.mouthWidth = 0.4f;
        if (t - dt <= 0.0f) {
          _shake = 4;
          for (int i = 0; i < 4; i++)
            spawn(P_PUFF, i < 2 ? 18 : 222, 225, (i < 2 ? 1 : -1) * frand(10, 30), frand(-30, -15), 1.8f, frand(9, 13),
                  _c->color565(170, 220, 90));
        }
      } else {
        _eyeStyle[0] = _eyeStyle[1] = EYE_HAPPY;
        g.blush = 1; g.mouthCurve = 0.9f; g.mouthOpen = 0.35f; g.mouthWidth = 1.0f; g.browTilt = 0.7f;
        _sway = 3 * sinf(t * 8);
      }
      break;
    case R_POKE:
      if (_pokeEye >= 0) { _eyeStyle[(int)_pokeEye] = EYE_CLOSED; }
      else { _eyeStyle[0] = _eyeStyle[1] = (t < 0.5f) ? EYE_CLOSED : EYE_HAPPY; }
      if (t < 0.5f) { g.mouthCurve = -0.5f; g.mouthWidth = 0.6f; g.browTilt = -0.8f; }
      else { g.mouthCurve = 1; g.mouthOpen = 0.5f + 0.3f * fabsf(sinf(t * 16)); g.blush = 0.7f; _bounce = 5; }
      break;
    case R_HELLO:
      if (t < 0.5f) { g.eyeOpenL = g.eyeOpenR = 0.05f + t * 1.8f; }
      else { _eyeStyle[0] = _eyeStyle[1] = (t > 0.8f && t < 1.6f) ? EYE_HAPPY : EYE_NORMAL; g.mouthCurve = 1; g.mouthOpen = 0.5f; _bounce = 6; }
      _sparkTimer -= dt;
      if (_sparkTimer <= 0 && t > 0.5f && t < 1.5f) {
        _sparkTimer = 0.12f;
        spawn(P_SPARKLE, frand(20, 220), frand(60, 220), 0, 0, 0.6f, frand(6, 11), _c->color565(255, 255, 160));
      }
      break;
    case R_WAKE:
      g.eyeScale = 1.2f; g.pupilSize = 0.6f; g.browH = 0.9f; _mouthStyle = M_O; g.mouthOpen = 0.5f; g.mouthWidth = 0.45f;
      break;
    case R_YAWN: {
      float y = sinf(u * PI_F);
      _mouthStyle = M_O; g.mouthOpen = 0.2f + 0.85f * y; g.mouthWidth = 0.5f + 0.25f * y;
      g.eyeOpenL = g.eyeOpenR = 1 - 0.85f * y; g.browH = 0.7f * y; g.pupilY = -0.2f;
      break;
    }
    case R_WHISTLE:
      _mouthStyle = M_O; g.mouthOpen = 0.25f; g.mouthWidth = 0.28f; g.pupilY = -0.7f; g.pupilX = 0.6f; g.browH = 0.5f;
      _sparkTimer -= dt;
      if (_sparkTimer <= 0 && t < 1.8f) {
        _sparkTimer = 0.45f;
        spawn(P_NOTE, 150, 165, frand(20, 40), frand(-55, -40), 1.6f, frand(8, 11), _white);
      }
      break;
    case R_LOOK:
      g.pupilX = (u < 0.33f) ? -1 : (u < 0.66f ? 1 : 0); g.pupilY = 0; g.browH = 0.2f; g.mouthCurve = 0.2f;
      break;
    case R_GOODNIGHT:
      g.eyeOpenL = g.eyeOpenR = clampf(1.0f - u * 1.2f, 0, 1);
      _mouthStyle = M_O; g.mouthOpen = 0.3f + 0.6f * sinf(clampf(u * 1.6f, 0, 1) * PI_F); g.mouthWidth = 0.45f;
      break;
    default: break;
  }

  // A sound's reaction keeps its pose for as long as the sound plays
  bool soundReaction = (_react < R_PICK_END) || _react == R_MASH;
  if (_rt >= _rdur && !(soundReaction && _audio)) {
    _react = R_NONE;
    _eyeStyle[0] = _eyeStyle[1] = EYE_NORMAL;
    _mouthStyle = M_NORMAL;
    _sway = 0; _tremble = 0; _lift = 0; _hat = 0;
    _nextIdle = frand(8, 18);
  }
}

void Face::update(float dt, float audioLevel, bool audioPlaying) {
  if (!_c) return;
  dt = clampf(dt, 0, 0.1f);
  _time += dt;
  _audio = audioPlaying;
  _audioLvl = audioLevel;
  _talk = approach(_talk, audioPlaying ? clampf(audioLevel * 1.8f, 0, 1) : 0, 25, dt);

  Params keepPupil = _tgt;
  modeTargets();
  _tgt.pupilX = keepPupil.pupilX;
  _tgt.pupilY = keepPupil.pupilY;
  if (_mode == FM_CONFIG || _mode == FM_SLEEPY || _mode == FM_EMPTY) modeTargets();

  idleBehaviour(dt);
  if (_mode == FM_AWAKE || _mode == FM_LOWBATT) applyReaction(dt);
  else if (_react == R_GOODNIGHT) applyReaction(dt);

  if (_glanceT > 0) {
    _glanceT -= dt;
    _tgt.pupilX = (_glanceKey - 2) * 0.45f;
    _tgt.pupilY = 0.8f;
  }

  // Mouth moves with the sound
  if (_talk > 0.02f && _mouthStyle != M_WAVY && _mouthStyle != M_TONGUE && _mouthStyle != M_KISS) {
    _tgt.mouthOpen = clampf(fmaxf(_tgt.mouthOpen * 0.6f, _talk), 0, 1);
  }
  if (_mouthStyle == M_TONGUE || _mouthStyle == M_KISS) _tgt.mouthOpen = 0;

  // Ease every parameter toward its target
  float sp = 14, spP = 22;
  _cur.eyeOpenL  = approach(_cur.eyeOpenL, _tgt.eyeOpenL, sp, dt);
  _cur.eyeOpenR  = approach(_cur.eyeOpenR, _tgt.eyeOpenR, sp, dt);
  _cur.eyeScale  = approach(_cur.eyeScale, _tgt.eyeScale, sp, dt);
  _cur.pupilX    = approach(_cur.pupilX, _tgt.pupilX, spP, dt);
  _cur.pupilY    = approach(_cur.pupilY, _tgt.pupilY, spP, dt);
  _cur.pupilSize = approach(_cur.pupilSize, _tgt.pupilSize, sp, dt);
  _cur.pupilCross= approach(_cur.pupilCross, _tgt.pupilCross, sp, dt);
  _cur.browH     = approach(_cur.browH, _tgt.browH, sp, dt);
  _cur.browTilt  = approach(_cur.browTilt, _tgt.browTilt, sp, dt);
  _cur.browAsym  = approach(_cur.browAsym, _tgt.browAsym, sp, dt);
  _cur.mouthOpen = approach(_cur.mouthOpen, _tgt.mouthOpen, 24, dt);
  _cur.mouthWidth= approach(_cur.mouthWidth, _tgt.mouthWidth, sp, dt);
  _cur.mouthCurve= approach(_cur.mouthCurve, _tgt.mouthCurve, sp, dt);
  _cur.blush     = approach(_cur.blush, _tgt.blush, 6, dt);

  _bounce = approach(_bounce, 0, 3, dt);
  _shake  = approach(_shake, 0, 5, dt);
  if (_react == R_NONE) { _sway = approach(_sway, 0, 6, dt); _tremble = 0; _lift = approach(_lift, 0, 8, dt); }
  if (_volT > 0) _volT -= dt;

  updateParticles(dt);
}

// ============================================================ drawing ===
void Face::arcLine(float cx, float cy, float halfW, float bulge, float thick, uint16_t c) {
  const int N = 10;
  float px = cx - halfW, py = cy;
  for (int i = 1; i <= N; i++) {
    float u = -1 + 2.0f * i / N;
    float x = cx + u * halfW, y = cy + bulge * (1 - u * u);
    _c->drawWideLine(px, py, x, y, thick, c);
    px = x; py = y;
  }
}

void Face::drawBackground() {
  _c->fillScreen(_body);
  // monster spots (matching the polka dots on the case)
  _c->fillCircle(22, 26, 13, _bodyDark);
  _c->fillCircle(44, 8, 6, _bodyDark);
  _c->fillCircle(220, 30, 9, _bodyDark);
  _c->fillCircle(16, 210, 10, _bodyDark);
  _c->fillCircle(222, 214, 15, _bodyDark);
  _c->fillCircle(200, 232, 6, _bodyDark);
}

void Face::drawHeart(float x, float y, float s, uint16_t c) {
  _c->fillCircle(x - s * 0.5f, y - s * 0.25f, s * 0.55f, c);
  _c->fillCircle(x + s * 0.5f, y - s * 0.25f, s * 0.55f, c);
  _c->fillTriangle(x - s * 1.03f, y - s * 0.05f, x + s * 1.03f, y - s * 0.05f, x, y + s * 1.1f, c);
}

void Face::drawStar(float x, float y, float r, float ang, uint16_t c) {
  float px[10], py[10];
  for (int i = 0; i < 10; i++) {
    float rr = (i & 1) ? r * 0.45f : r;
    float a = ang + i * PI_F / 5 - PI_F / 2;
    px[i] = x + rr * cosf(a); py[i] = y + rr * sinf(a);
  }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    _c->fillTriangle(x, y, px[i], py[i], px[j], py[j], c);
  }
}

void Face::drawNote(float x, float y, float s, uint16_t c) {
  _c->fillEllipse(x, y, s * 0.75f, s * 0.55f, c);
  _c->drawWideLine(x + s * 0.65f, y, x + s * 0.65f, y - s * 2.4f, s * 0.14f, c);
  _c->drawWideLine(x + s * 0.65f, y - s * 2.4f, x + s * 1.5f, y - s * 1.7f, s * 0.16f, c);
}

void Face::drawPuff(float x, float y, float s, uint16_t c) {
  _c->fillCircle(x - s * 0.55f, y + s * 0.1f, s * 0.6f, c);
  _c->fillCircle(x + s * 0.55f, y + s * 0.1f, s * 0.6f, c);
  _c->fillCircle(x, y - s * 0.3f, s * 0.75f, c);
}

void Face::drawEye(int idx, float ex, float ey) {
  float R = EYE_R * _cur.eyeScale;
  float open = clampf(idx ? _cur.eyeOpenR : _cur.eyeOpenL, 0, 1.2f);
  if (_blinkT > 0) {
    float b = _blinkT / _blinkDur;                 // 1 -> 0
    open *= fabsf(b * 2 - 1);
  }
  EyeStyle st = _eyeStyle[idx];

  switch (st) {
    case EYE_SHADES: return;                       // drawn as a pair in drawShades()
    case EYE_HAPPY:  arcLine(ex, ey + 6, R * 0.8f, -R * 0.45f, 4.5f, _ink); return;
    case EYE_CLOSED: arcLine(ex, ey - 2, R * 0.8f, R * 0.35f, 4.5f, _ink); return;
    case EYE_HEART: {
      float pulse = 1.0f + 0.12f * sinf(_time * 12);
      drawHeart(ex, ey - 4, R * 0.62f * pulse + 3, _ink);
      drawHeart(ex, ey - 4, R * 0.62f * pulse, _c->color565(240, 40, 80));
      _c->fillCircle(ex - R * 0.32f, ey - R * 0.32f, R * 0.12f, _white);
      return;
    }
    case EYE_STAR: {
      float pulse = 1.0f + 0.1f * sinf(_time * 14 + idx);
      drawStar(ex, ey, R * 0.95f * pulse, _time * 2, _ink);
      drawStar(ex, ey, R * 0.95f * pulse - 5, _time * 2, _c->color565(255, 220, 50));
      return;
    }
    case EYE_SPIRAL: {
      _c->fillCircle(ex, ey, R + 3, _ink);
      _c->fillCircle(ex, ey, R, _white);
      float rot = _spiralRot * (idx ? -1 : 1);
      float px = ex, py = ey;
      for (int i = 1; i <= 48; i++) {
        float a = i * (4.2f * PI_F / 48);
        float r = (R - 5) * i / 48.0f;
        float x = ex + r * cosf(a + rot), y = ey + r * sinf(a + rot);
        _c->drawWideLine(px, py, x, y, 2.4f, _ink);
        px = x; py = y;
      }
      return;
    }
    default: break;
  }

  if (open < 0.08f) { arcLine(ex, ey + 2, R * 0.8f, R * 0.25f, 4.5f, _ink); return; }

  // sclera with outline (full size, eyelid drawn on top)
  _c->fillEllipse(ex, ey, R + 3, R + 3, _ink);
  _c->fillEllipse(ex, ey, R, R, _white);

  // pupil
  float pr = R * 0.46f * _cur.pupilSize;
  float pX = clampf(_cur.pupilX + (idx ? -_cur.pupilCross : _cur.pupilCross), -1, 1);
  float pY = clampf(_cur.pupilY, -1, 1);
  float maxOff = R - pr - 3;
  float dx = pX * maxOff, dy = pY * maxOff;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > maxOff) { dx *= maxOff / d; dy *= maxOff / d; }
  float px = ex + dx, py = ey + dy;
  _c->fillCircle(px, py, pr, _ink);
  _c->fillCircle(px - pr * 0.35f, py - pr * 0.38f, pr * 0.32f + 1, _white);
  _c->fillCircle(px + pr * 0.35f, py + pr * 0.35f, pr * 0.13f + 1, _white);

  // eyelid coming down from the top
  if (open < 0.98f) {
    float lidY = ey - R + 2 * R * (1 - clampf(open, 0, 1));
    _c->fillRect(ex - R - 4, ey - R - 6, 2 * R + 9, lidY - (ey - R - 6), _body);
    float h = clampf((lidY - ey) / (R + 3), -1, 1);
    float hw = (R + 3) * sqrtf(1 - h * h);
    _c->drawWideLine(ex - hw, lidY, ex + hw, lidY, 2.5f, _ink);
  }
}

void Face::drawShades(float ox, float oy) {
  float y = EYE_Y + oy - 6;
  for (int i = 0; i < 2; i++) {
    float ex = 120 + (i ? EYE_DX : -EYE_DX) + ox;
    _c->fillRoundRect(ex - 40, y - 22, 80, 46, 18, _ink);
    _c->drawWideLine(ex - 22, y - 8, ex - 8, y - 18, 2.5f, _c->color565(140, 140, 170));
    _c->drawWideLine(ex - 18, y + 4, ex + 2, y - 16, 2.0f, _c->color565(100, 100, 130));
  }
  _c->drawWideLine(120 - EYE_DX - 40 + ox, y - 20, 120 + EYE_DX + 40 + ox, y - 20, 3.5f, _ink);
}

void Face::drawBrow(int idx, float ex, float ey) {
  if (_mode == FM_SLEEP) return;
  float R = EYE_R * (_eyeStyle[idx] == EYE_SHADES ? 1.0f : _cur.eyeScale);
  float by = ey - R - 15 - _cur.browH * 10 - (idx ? _cur.browAsym * 12 : 0);
  if (_eyeStyle[idx] == EYE_SHADES) by -= 6;
  float tilt = _cur.browTilt * 8;
  float sgn = idx ? 1 : -1;                      // outer side direction
  float outerX = ex + sgn * 24, innerX = ex - sgn * 20;
  float outerY = by + tilt, innerY = by - tilt;
  _c->drawWideLine(outerX, outerY, innerX, innerY, 5, _ink);
}

void Face::drawMouth(float mx, float my) {
  float Wd = 52 * _cur.mouthWidth;
  float c = _cur.mouthCurve * 14;
  float o = clampf(_cur.mouthOpen, 0, 1.1f);

  if (_mouthStyle == M_WAVY) {
    float px = mx - Wd, py = my;
    float speed = (_react == R_SCARED) ? 30 : 8;   // scared: chattering teeth
    for (int i = 1; i <= 24; i++) {
      float u = -1 + 2.0f * i / 24;
      float x = mx + u * Wd, y = my + 6 * sinf(u * 3.2f * PI_F + _time * speed);
      _c->drawWideLine(px, py, x, y, 3.5f, _ink);
      px = x; py = y;
    }
    return;
  }

  if (_mouthStyle == M_O) {
    float rx = 10 + 30 * _cur.mouthWidth, ry = 6 + 28 * o;
    _c->fillEllipse(mx, my + 4, rx + 3, ry + 3, _ink);
    _c->fillEllipse(mx, my + 4, rx, ry, _mouthDark);
    if (ry > 12) _c->fillEllipse(mx, my + 4 + ry * 0.55f, rx * 0.6f, ry * 0.35f, _tongue);
    return;
  }

  if (_mouthStyle == M_KISS) {
    float kx = mx + 8, ky = my + 2, pulse = 1 + 0.08f * sinf(_time * 10);
    _c->fillEllipse(kx, ky, 15 * pulse, 13 * pulse, _ink);
    _c->fillEllipse(kx, ky - 4, 11 * pulse, 6 * pulse, _c->color565(240, 60, 100));
    _c->fillEllipse(kx, ky + 4, 11 * pulse, 6 * pulse, _c->color565(240, 60, 100));
    _c->drawWideLine(kx - 7, ky, kx + 7, ky, 1.5f, _ink);
    return;
  }

  if (o < 0.06f) {
    arcLine(mx, my, Wd, c, 3.5f, _ink);
  } else {
    // Filled open mouth built from vertical spans
    for (int pass = 0; pass < 3; pass++) {
      for (int x = (int)-Wd - 3; x <= (int)Wd + 3; x++) {
        float u = x / Wd;
        if (u < -1.06f || u > 1.06f) continue;
        float uu = clampf(u, -1, 1);
        float line = my + c * (1 - uu * uu);
        float rnd = sqrtf(fmaxf(0, 1 - uu * uu));
        float top = line - o * 6 * rnd;
        float bot = line + o * 44 * rnd;
        if (pass == 0) {            // outline
          _c->drawFastVLine(mx + x, top - 3, bot - top + 6, _ink);
        } else if (pass == 1) {     // inside
          if (fabsf(u) > 1) continue;
          _c->drawFastVLine(mx + x, top, bot - top, _mouthDark);
        } else {                    // tongue
          if (fabsf(u) > 0.62f || o < 0.3f) continue;
          float tu = u / 0.62f;
          float tTop = bot - o * 18 * sqrtf(fmaxf(0, 1 - tu * tu)) - 2;
          if (tTop < bot) _c->drawFastVLine(mx + x, fmaxf(tTop, top), bot - fmaxf(tTop, top), _tongue);
        }
      }
    }
  }

  // Two little monster fangs
  for (int s = -1; s <= 1; s += 2) {
    float fx = mx + s * Wd * 0.45f;
    float u = s * 0.45f;
    float top = my + c * (1 - u * u) - (o >= 0.06f ? o * 6 * sqrtf(1 - u * u) : 0);
    float len = 9 + 4 * clampf(o * 3, 0, 1);
    _c->fillTriangle(fx - 6, top - 1, fx + 6, top - 1, fx, top + len, _white);
  }

  if (_mouthStyle == M_TONGUE) {
    float wig = 1.0f + 0.35f * _talk + 0.08f * sinf(_time * 18);
    float tx = mx + 10 + 3 * sinf(_time * 9), ty = my + c + 12 * wig;
    _c->fillEllipse(tx, ty, 15, 17 * wig + 2, _ink);
    _c->fillEllipse(tx, ty, 12, 17 * wig - 1, _tongue);
    _c->drawWideLine(tx, ty - 8 * wig, tx, ty + 9 * wig, 1.5f, _c->color565(220, 70, 100));
    arcLine(mx, my, Wd, c, 3.5f, _ink);
  }
}

void Face::drawHat(float ox, float oy) {
  if (_hat <= 0) return;
  float bx = 120 + ox, by = 44 + oy;                 // small hat between the brows, slightly tilted
  float lx = bx - 24, ly = by - 2, rx = bx + 24, ry = by + 3, tx = bx + 5, ty = by - 36;
  _c->fillTriangle(lx - 3, ly + 2, rx + 3, ry + 2, tx, ty - 4, _ink);
  _c->fillTriangle(lx, ly, rx, ry, tx, ty, _c->color565(80, 160, 255));
  for (int i = 1; i <= 3; i++) {                     // stripes
    float k = i / 4.0f;
    float ax = lx + (tx - lx) * k, ay = ly + (ty - ly) * k;
    float cx2 = rx + (tx - rx) * k, cy2 = ry + (ty - ry) * k;
    _c->drawWideLine(ax, ay, cx2, cy2, 2.5f, i == 2 ? _c->color565(255, 230, 70) : _c->color565(255, 90, 140));
  }
  _c->fillCircle(tx, ty, 8, _ink);
  _c->fillCircle(tx, ty, 6, rainbow(_time * 0.8f));
}

void Face::drawParticles() {
  for (int i = 0; i < MAXP; i++) {
    Particle &p = _p[i];
    if (!p.alive) continue;
    float a = p.life / p.maxLife;
    float grow = 1.0f + (1 - a) * 0.6f;
    switch (p.type) {
      case P_HEART:   drawHeart(p.x, p.y, p.size * (0.6f + 0.4f * a), p.color); break;
      case P_STAR:    drawStar(p.x, p.y, p.size, p.angle, p.color); break;
      case P_NOTE:    drawNote(p.x, p.y, p.size, p.color); break;
      case P_PUFF:    drawPuff(p.x, p.y, p.size * (a > 0.25f ? 1 : a * 4), p.color); break;
      case P_SPARKLE: {
        float s = p.size * sinf(a * PI_F);
        _c->drawWideLine(p.x - s, p.y, p.x + s, p.y, 1.5f, p.color);
        _c->drawWideLine(p.x, p.y - s, p.x, p.y + s, 1.5f, p.color);
        break;
      }
      case P_SWEAT:
      case P_TEAR:
        _c->fillCircle(p.x, p.y, p.size, p.color);
        _c->fillTriangle(p.x - p.size, p.y - 1, p.x + p.size, p.y - 1, p.x, p.y - p.size * 2.2f, p.color);
        break;
      case P_DROP:
        _c->fillCircle(p.x, p.y, p.size, p.color);
        break;
      case P_CONFETTI: {
        float ca = cosf(p.angle * 2), sa = sinf(p.angle * 2);
        float hw = p.size, hh = p.size * 0.45f;
        float x0 = p.x - ca * hw + sa * hh, y0 = p.y - sa * hw - ca * hh;
        float x1 = p.x + ca * hw + sa * hh, y1 = p.y + sa * hw - ca * hh;
        float x2 = p.x + ca * hw - sa * hh, y2 = p.y + sa * hw + ca * hh;
        float x3 = p.x - ca * hw - sa * hh, y3 = p.y - sa * hw + ca * hh;
        _c->fillTriangle(x0, y0, x1, y1, x2, y2, p.color);
        _c->fillTriangle(x0, y0, x2, y2, x3, y3, p.color);
        break;
      }
      case P_ORBIT: {
        float ox = 120 + _sway + 78 * cosf(p.angle), oy = 26 + 14 * sinf(p.angle);
        drawStar(ox, oy, p.size, p.angle * 2, _ink);
        drawStar(ox, oy, p.size - 2.5f, p.angle * 2, p.color);
        break;
      }
      case P_BUBBLE: {
        _c->setFont(&fonts::FreeSansBold12pt7b);
        _c->setTextSize(1);
        int w = _c->textWidth(p.text) + 24;
        float pop = clampf((p.maxLife - p.life) * 6, 0, 1);
        int bw = w * pop, bh = 34 * pop;
        if (bw < 8) break;
        _c->fillRoundRect(p.x - bw / 2 - 2, p.y - bh / 2 - 2, bw + 4, bh + 4, 14, _ink);
        _c->fillRoundRect(p.x - bw / 2, p.y - bh / 2, bw, bh, 12, _white);
        _c->fillTriangle(p.x - 10, p.y + bh / 2 - 2, p.x + 6, p.y + bh / 2 - 2, p.x - 4, p.y + bh / 2 + 12, _white);
        if (pop >= 1) {
          _c->setTextDatum(textdatum_t::middle_center);
          _c->setTextColor(_ink);
          _c->drawString(p.text, p.x, p.y + 1);
        }
        break;
      }
      case P_Z:
      case P_TEXT:
        _c->setFont(p.type == P_Z ? &fonts::FreeSansBold18pt7b : &fonts::FreeSansBold24pt7b);
        _c->setTextDatum(textdatum_t::middle_center);
        _c->setTextSize(p.size * (p.type == P_Z ? grow : 1.0f) * 0.7f);
        _c->setTextColor(_ink);
        _c->drawString(p.text, p.x + 2, p.y + 2);
        _c->setTextColor(p.color);
        _c->drawString(p.text, p.x, p.y);
        _c->setTextSize(1);
        break;
    }
  }
}

void Face::drawOverlays() {
  // Battery badge (top right)
  bool low = _battMv < 3450;
  if (_battShow || _charging || low) {
    int x = 196, y = 8;
    uint16_t frame = _ink;
    uint16_t fill = low ? _c->color565(240, 60, 50) : _c->color565(80, 220, 90);
    if (!(low && !_charging && fmodf(_time, 1.0f) < 0.4f)) {
      _c->fillRoundRect(x - 2, y - 2, 38, 20, 5, _white);
      _c->drawRoundRect(x, y, 30, 16, 3, frame);
      _c->fillRect(x + 30, y + 5, 3, 6, frame);
      float pct = clampf((_battMv - 3300) / 900.0f, 0.05f, 1);
      _c->fillRect(x + 2, y + 2, (int)(26 * pct), 12, fill);
      if (_charging) {
        _c->fillTriangle(x + 17, y + 1, x + 10, y + 9, x + 15, y + 9, _c->color565(255, 210, 0));
        _c->fillTriangle(x + 13, y + 15, x + 20, y + 7, x + 15, y + 7, _c->color565(255, 210, 0));
      }
    }
  }

  // Volume bar
  if (_volT > 0 && _volShow >= 0) {
    _c->fillRoundRect(30, 196, 180, 34, 10, _ink);
    int sx = 44, sy = 213;                       // little speaker icon
    _c->fillRect(sx, sy - 5, 6, 10, _white);
    _c->fillTriangle(sx + 4, sy, sx + 14, sy - 10, sx + 14, sy + 10, _white);
    for (int i = 0; i < 10; i++) {
      int h = 6 + i * 1.6f;
      uint16_t col = (i < _volShow) ? _c->color565(120, 230, 120) : _c->color565(90, 80, 110);
      _c->fillRect(70 + i * 13, sy + 9 - h, 9, h, col);
    }
  }

  // Setup-mode panel
  if (_mode == FM_CONFIG) {
    _c->fillRoundRect(8, 150, 224, 84, 12, _ink);
    _c->setFont(&fonts::FreeSansBold9pt7b);
    _c->setTextSize(1);
    _c->setTextDatum(textdatum_t::middle_center);
    _c->setTextColor(_c->color565(255, 220, 90));
    _c->drawString(_cfg[0], 120, 168);
    _c->setTextColor(_white);
    _c->drawString(_cfg[1], 120, 192);
    _c->setTextColor(_c->color565(140, 220, 255));
    _c->drawString(_cfg[2], 120, 216);
  }

  if (_mode == FM_EMPTY) {
    _c->setFont(&fonts::FreeSansBold9pt7b);
    _c->setTextDatum(textdatum_t::middle_center);
    _c->setTextColor(_white);
    _c->drawString("charge me!", 120, 224);
  }
}

void Face::render() {
  if (!_c) return;
  drawBackground();

  float ox = _sway + (_shake > 0.3f ? sinf(_time * 70) * _shake : 0) + (_tremble > 0 ? sinf(_time * 90) * _tremble : 0);
  float oy = -fabsf(sinf(_time * 13)) * _bounce + _lift;
  if (_mode == FM_SLEEP) oy += 3 * sinf(_time * 1.6f);   // breathing

  if (_cur.blush > 0.25f) {
    uint16_t b = _cur.blush > 0.5f ? _blushC : _c->color565(255, 170, 170);
    _c->fillEllipse(120 - EYE_DX - 12 + ox, EYE_Y + 50 + oy, 18, 10, b);
    _c->fillEllipse(120 + EYE_DX + 12 + ox, EYE_Y + 50 + oy, 18, 10, b);
  }

  for (int i = 0; i < 2; i++) {
    float ex = 120 + (i ? EYE_DX : -EYE_DX) + ox, ey = EYE_Y + oy;
    drawEye(i, ex, ey);
  }
  if (_eyeStyle[0] == EYE_SHADES) drawShades(ox, oy);
  for (int i = 0; i < 2; i++) drawBrow(i, 120 + (i ? EYE_DX : -EYE_DX) + ox, EYE_Y + oy);
  if (_mode != FM_CONFIG) drawMouth(120 + ox, MOUTH_Y + oy);
  drawHat(ox, oy);
  drawParticles();
  drawOverlays();
}
