#include "audio.h"
#include "config.h"
#include "es8311.h"
#include "settings.h"
#include <Wire.h>
#include <FS.h>
#include <SD_MMC.h>
#include <atomic>
#include "driver/i2s_std.h"

namespace Audio {

static const uint32_t BASE_RATE = 22050;
static const int CHUNK = 256;              // frames per write (~12 ms at 22 kHz)

static ES8311 codec;
static i2s_chan_handle_t tx = nullptr;
static uint32_t curRate = 0;
static QueueHandle_t q = nullptr;
static std::atomic<bool> playing{false};
static std::atomic<float> lvl{0.0f};
static int16_t lastSample = 0;

enum CmdType : uint8_t { CMD_FILE, CMD_FX, CMD_STOP };
struct Cmd { CmdType type; uint8_t fx; char path[120]; };

// ---------------------------------------------------------------- I2S ------
static void setRate(uint32_t rate) {
  if (rate == curRate) return;
  i2s_channel_disable(tx);
  i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(rate);
  clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  i2s_channel_reconfig_std_clock(tx, &clk);
  i2s_channel_enable(tx);
  curRate = rate;
}

static bool i2sInit() {
  i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cc.dma_desc_num = 4;
  cc.dma_frame_num = 256;
  cc.auto_clear = true;                    // DMA plays silence when we stop feeding it
  if (i2s_new_channel(&cc, &tx, nullptr) != ESP_OK) return false;

  i2s_std_config_t sc = {};
  sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BASE_RATE);
  sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  sc.gpio_cfg.mclk = (gpio_num_t)I2S_MCLK;
  sc.gpio_cfg.bclk = (gpio_num_t)I2S_BCLK;
  sc.gpio_cfg.ws   = (gpio_num_t)I2S_LRCK;
  sc.gpio_cfg.dout = (gpio_num_t)I2S_DOUT;
  sc.gpio_cfg.din  = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(tx, &sc) != ESP_OK) return false;
  i2s_channel_enable(tx);
  curRate = BASE_RATE;
  return true;
}

static void writeFrames(int16_t *stereo, int frames) {
  size_t written;
  i2s_channel_write(tx, stereo, frames * 4, &written, pdMS_TO_TICKS(200));
}

// Short ramp to zero so an interrupted sound doesn't click.
static void rampOut() {
  int16_t buf[64 * 2];
  for (int i = 0; i < 64; i++) {
    int16_t s = (int16_t)(lastSample * (63 - i) / 64);
    buf[i * 2] = buf[i * 2 + 1] = s;
  }
  writeFrames(buf, 64);
  lastSample = 0;
}

// Peak meter with fast attack, slower release.
static void meter(const int16_t *stereo, int frames) {
  int peak = 0;
  for (int i = 0; i < frames; i++) {
    int a = abs(stereo[i * 2]);
    if (a > peak) peak = a;
  }
  float p = peak / 32768.0f;
  float cur = lvl.load();
  lvl.store(p > cur ? p : cur * 0.80f + p * 0.20f);
}

// ----------------------------------------------------------- WAV files -----
struct WavInfo { uint32_t rate; uint16_t ch; uint16_t bits; uint32_t dataLen; };

static bool parseWav(File &f, WavInfo &w) {
  uint8_t h[12];
  if (f.read(h, 12) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) return false;
  bool gotFmt = false;
  while (f.available()) {
    uint8_t ch[8];
    if (f.read(ch, 8) != 8) return false;
    uint32_t len = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
    if (!memcmp(ch, "fmt ", 4)) {
      uint8_t fm[40] = {0};
      f.read(fm, min<uint32_t>(len, sizeof(fm)));
      if (len > sizeof(fm)) f.seek(f.position() + len - sizeof(fm));
      uint16_t fmt = fm[0] | (fm[1] << 8);
      w.ch   = fm[2] | (fm[3] << 8);
      w.rate = fm[4] | (fm[5] << 8) | (fm[6] << 16) | ((uint32_t)fm[7] << 24);
      w.bits = fm[14] | (fm[15] << 8);
      if (fmt == 0xFFFE && len >= 26) fmt = fm[24] | (fm[25] << 8);   // extensible
      if (fmt != 1) return false;                                     // PCM only
      gotFmt = true;
    } else if (!memcmp(ch, "data", 4)) {
      w.dataLen = len;
      return gotFmt && (w.bits == 16 || w.bits == 8) && (w.ch == 1 || w.ch == 2)
             && w.rate >= 8000 && w.rate <= 48000;
    } else {
      f.seek(f.position() + len + (len & 1));
    }
  }
  return false;
}

// Returns true if interrupted by a new command (left in *pending).
static bool playWav(const char *path, Cmd *pending) {
  File f = SD_MMC.open(path);
  if (!f) { log_w("cannot open %s", path); return false; }
  WavInfo w;
  if (!parseWav(f, w)) { log_w("unsupported wav %s", path); f.close(); return false; }
  setRate(w.rate);

  const int bpf = w.ch * (w.bits / 8);
  static uint8_t raw[CHUNK * 4];
  static int16_t out[CHUNK * 2];
  uint32_t left = w.dataLen;
  uint32_t n = 0;
  const uint32_t fadeIn = w.rate / 200;   // 5 ms

  while (left > 0) {
    if (xQueueReceive(q, pending, 0) == pdTRUE) { f.close(); rampOut(); return true; }
    int want = min<uint32_t>(CHUNK * bpf, left);
    int got = f.read(raw, want);
    if (got <= 0) break;
    left -= got;
    int frames = got / bpf;
    for (int i = 0; i < frames; i++) {
      int32_t s;
      const uint8_t *p = raw + i * bpf;
      if (w.bits == 16) {
        s = (int16_t)(p[0] | (p[1] << 8));
        if (w.ch == 2) s = (s + (int16_t)(p[2] | (p[3] << 8))) / 2;
      } else {
        s = ((int)p[0] - 128) << 8;
        if (w.ch == 2) s = (s + (((int)p[1] - 128) << 8)) / 2;
      }
      if (n < fadeIn) s = s * (int32_t)n / (int32_t)fadeIn;
      n++;
      out[i * 2] = out[i * 2 + 1] = (int16_t)s;
    }
    if (frames > 0) lastSample = out[(frames - 1) * 2];
    meter(out, frames);
    writeFrames(out, frames);
  }
  f.close();
  rampOut();
  return false;
}

// ------------------------------------------------------ synthesized FX -----
struct Note { float hz; float dur; };

static float noteEnv(float t, float dur) {
  float a = min(1.0f, t / 0.008f);
  float r = min(1.0f, (dur - t) / 0.03f);
  return max(0.0f, min(a, r));
}

// Fast sine of a phase given in turns (1.0 = one full cycle). Wraps any
// phase, so it never feeds sinf() huge arguments (newlib's slow path for
// |x| > ~200 rad starved the audio task: crackle, then a watchdog reset).
static float SIN_LUT[1025];
static void initLut() {
  for (int i = 0; i <= 1024; i++) SIN_LUT[i] = sinf(i * (6.2831853f / 1024.0f));
}
static inline float osc(float turns) {
  turns -= floorf(turns);
  float x = turns * 1024.0f;
  int i = (int)x;
  if (i > 1023) i = 1023;
  float f = x - i;
  return SIN_LUT[i] + (SIN_LUT[i + 1] - SIN_LUT[i]) * f;
}
static inline void wrap(float &ph) { if (ph >= 1.0f) ph -= floorf(ph); }

static float fxDuration(uint8_t fx) {
  switch (fx) {
    case FX_GIGGLE:    return 0.80f;
    case FX_SLIDE_UP:  return 0.60f;
    case FX_RASPBERRY: return 0.65f;
    case FX_LOVE:      return 0.90f;
    case FX_DIZZY:     return 1.00f;
    case FX_BLIP:      return 0.07f;
    case FX_HELLO:     return 0.75f;
    case FX_GOODNIGHT: return 0.90f;
    case FX_POP:       return 0.12f;
    case FX_BOING:     return 0.70f;
  }
  return 0.3f;
}

static float seqFreq(const Note *seq, int n, float t, float &local, float &nd) {
  float acc = 0;
  for (int i = 0; i < n; i++) {
    if (t < acc + seq[i].dur) { local = t - acc; nd = seq[i].dur; return seq[i].hz; }
    acc += seq[i].dur;
  }
  local = 0; nd = 1; return 0;
}

// ------------------------------------------------------- sung tunes -------
// Public-domain nursery tunes. {midi note, length in eighths}; C5 = 72.
struct SongNote { uint8_t midi; uint8_t eighths; };
static const SongNote SONG_TWINKLE[] = {
  {72,2},{72,2},{79,2},{79,2},{81,2},{81,2},{79,4},{77,2},{77,2},{76,2},{76,2},{74,2},{74,2},{72,4}};
static const SongNote SONG_JACQUES[] = {
  {72,2},{74,2},{76,2},{72,2},{72,2},{74,2},{76,2},{72,2},{76,2},{77,2},{79,4},{76,2},{77,2},{79,4},
  {79,1},{81,1},{79,1},{77,1},{76,2},{72,2},{79,1},{81,1},{79,1},{77,1},{76,2},{72,2},
  {72,2},{67,2},{72,4},{72,2},{67,2},{72,4}};
static const SongNote SONG_MARY[] = {
  {76,2},{74,2},{72,2},{74,2},{76,2},{76,2},{76,4},{74,2},{74,2},{74,4},{76,2},{79,2},{79,4},
  {76,2},{74,2},{72,2},{74,2},{76,2},{76,2},{76,2},{76,2},{74,2},{74,2},{76,2},{74,2},{72,8}};
static const SongNote SONG_ODE[] = {
  {76,2},{76,2},{77,2},{79,2},{79,2},{77,2},{76,2},{74,2},{72,2},{72,2},{74,2},{76,2},{76,3},{74,1},{74,4}};
static const SongNote SONG_MACDONALD[] = {
  {72,2},{72,2},{72,2},{67,2},{69,2},{69,2},{67,4},{76,2},{76,2},{74,2},{74,2},{72,6}};

struct Song { const SongNote *notes; int n; float eighth; };
static const Song SONGS[NUM_SONGS] = {
  {SONG_TWINKLE, sizeof(SONG_TWINKLE) / sizeof(SongNote), 0.21f},
  {SONG_JACQUES, sizeof(SONG_JACQUES) / sizeof(SongNote), 0.18f},
  {SONG_MARY, sizeof(SONG_MARY) / sizeof(SongNote), 0.19f},
  {SONG_ODE, sizeof(SONG_ODE) / sizeof(SongNote), 0.21f},
  {SONG_MACDONALD, sizeof(SONG_MACDONALD) / sizeof(SongNote), 0.19f},
};

// A cute "la-la" monster voice: soft harmonics, a little scoop into each
// note, vibrato on long notes, and short gaps so the mouth closes between syllables.
static bool playSong(int idx, Cmd *pending) {
  setRate(BASE_RATE);
  const Song &song = SONGS[idx];
  const float sr = BASE_RATE;
  static int16_t out[CHUNK * 2];
  float ph = 0;
  int k = 0;
  for (int n = 0; n < song.n; n++) {
    const float dur = song.notes[n].eighths * song.eighth;
    const float gate = dur * 0.86f;
    const float hz = 440.0f * powf(2.0f, (song.notes[n].midi - 69) / 12.0f);
    const int total = (int)(dur * sr);
    for (int i = 0; i < total; i++) {
      float t = i / sr;
      float scoop = (t < 0.04f) ? powf(2.0f, -(1.0f - t / 0.04f) / 12.0f) : 1.0f;
      float vib = (t > 0.15f) ? 1.0f + 0.012f * osc(t * 5.5f) : 1.0f;
      ph += hz * scoop * vib / sr; wrap(ph);
      wrap(ph);
      float s = 0.55f * osc(ph) + 0.30f * osc(2 * ph) + 0.15f * osc(3 * ph);
      float env = 0;
      if (t < gate) env = fminf(1.0f, t / 0.02f) * fminf(1.0f, (gate - t) / 0.05f) * (1.0f - 0.2f * (t / gate));
      out[k * 2] = out[k * 2 + 1] = (int16_t)(s * env * 0.42f * 32767.0f);
      if (++k == CHUNK) {
        if (xQueueReceive(q, pending, 0) == pdTRUE) { lastSample = out[(k - 1) * 2]; rampOut(); return true; }
        lastSample = out[(k - 1) * 2];
        meter(out, k);
        writeFrames(out, k);
        k = 0;
        taskYIELD();
      }
    }
  }
  if (k) { meter(out, k); writeFrames(out, k); }
  rampOut();
  return false;
}

static bool playFx(uint8_t fx, Cmd *pending) {
  if (fx >= FX_SONG_0 && fx < FX_SONG_0 + NUM_SONGS) return playSong(fx - FX_SONG_0, pending);
  setRate(BASE_RATE);
  const float sr = BASE_RATE;
  const float dur = fxDuration(fx);
  const int total = (int)(dur * sr);
  static int16_t out[CHUNK * 2];
  float ph = 0, ph2 = 0;
  uint32_t rng = 0x1234567;

  static const Note giggle[] = {{784,0.09f},{988,0.09f},{1175,0.09f},{988,0.09f},{1175,0.09f},{1319,0.09f},{1568,0.26f}};
  static const Note hello[]  = {{392,0.12f},{523,0.12f},{659,0.12f},{784,0.39f}};
  static const Note night[]  = {{784,0.2f},{659,0.2f},{523,0.2f},{392,0.3f}};

  for (int i = 0; i < total;) {
    if (xQueueReceive(q, pending, 0) == pdTRUE) { rampOut(); return true; }
    int frames = min(CHUNK, total - i);
    for (int k = 0; k < frames; k++, i++) {
      float t = i / sr;
      float s = 0, hz = 0, env = 1, local, nd;
      switch (fx) {
        case FX_GIGGLE:
          hz = seqFreq(giggle, 7, t, local, nd);
          env = noteEnv(local, nd);
          ph += hz / sr; wrap(ph);
          s = (ph < 0.5f ? 0.6f : -0.6f) * 0.5f + 0.5f * osc(ph);
          break;
        case FX_SLIDE_UP:
          hz = 300.0f * powf(5.0f, t / dur) * (1.0f + 0.02f * osc(t * 9));
          env = noteEnv(t, dur);
          ph += hz / sr; wrap(ph);
          s = 1.0f - 4.0f * fabsf(fmodf(ph, 1.0f) - 0.5f);   // triangle
          break;
        case FX_RASPBERRY: {
          hz = 85.0f + 25.0f * osc(t * 3);
          ph += hz / sr; wrap(ph);
          rng = rng * 1664525u + 1013904223u;
          float noise = ((rng >> 9) & 0xFFFF) / 32768.0f - 1.0f;
          float saw = 2.0f * fmodf(ph, 1.0f) - 1.0f;
          float flutter = 0.5f + 0.5f * osc(t * 28);
          s = (saw * 0.7f + noise * 0.3f) * flutter;
          env = noteEnv(t, dur);
          break;
        }
        case FX_LOVE:
          hz = (t < 0.35f ? 659.0f + 400.0f * (t / 0.35f) : 1047.0f - 150.0f * ((t - 0.35f) / 0.55f));
          hz *= 1.0f + 0.025f * osc(t * 6);
          env = noteEnv(t, dur);
          ph += hz / sr; wrap(ph);
          ph2 += hz * 2.0f / sr; wrap(ph2);
          s = 0.8f * osc(ph) + 0.2f * osc(ph2);
          break;
        case FX_DIZZY:
          hz = 650.0f - 380.0f * (t / dur) + 140.0f * osc(t * 7);
          env = noteEnv(t, dur) * (0.6f + 0.4f * osc(t * 5));
          ph += hz / sr; wrap(ph);
          s = osc(ph) * 0.8f + 0.2f * osc(ph * 3);
          break;
        case FX_BLIP:
          ph += 880.0f / sr; wrap(ph);
          env = noteEnv(t, dur);
          s = osc(ph);
          break;
        case FX_HELLO:
          hz = seqFreq(hello, 4, t, local, nd);
          env = noteEnv(local, nd);
          ph += hz / sr; wrap(ph);
          s = osc(ph) * 0.7f + 0.3f * osc(ph * 2);
          break;
        case FX_GOODNIGHT:
          hz = seqFreq(night, 4, t, local, nd);
          env = noteEnv(local, nd) * 0.7f;
          ph += hz / sr; wrap(ph);
          s = osc(ph);
          break;
        case FX_POP:
          hz = 400.0f + 700.0f * (t / dur);
          env = noteEnv(t, dur);
          ph += hz / sr; wrap(ph);
          s = osc(ph);
          break;
        case FX_BOING:
          hz = 110.0f + 420.0f * expf(-5.0f * t) * (1.0f + 0.3f * osc(t * 16));
          env = expf(-3.0f * t) * min(1.0f, t / 0.005f);
          ph += hz / sr; wrap(ph);
          s = osc(ph);
          break;
      }
      int16_t v = (int16_t)(s * env * 0.55f * 32767.0f);
      out[k * 2] = out[k * 2 + 1] = v;
    }
    lastSample = out[(frames - 1) * 2];
    meter(out, frames);
    writeFrames(out, frames);
  }
  rampOut();
  return false;
}

// --------------------------------------------------------------- task ------
static void task(void *) {
  Cmd c;
  for (;;) {
    if (xQueueReceive(q, &c, portMAX_DELAY) != pdTRUE) continue;
    bool again = true;
    while (again) {
      again = false;
      if (c.type == CMD_STOP) break;
      playing = true;
      Cmd next;
      bool interrupted = (c.type == CMD_FILE) ? playWav(c.path, &next) : playFx(c.fx, &next);
      if (interrupted) { c = next; again = true; }
    }
    playing = false;
    lvl = 0;
  }
}

// ---------------------------------------------------------------- API ------
bool begin() {
  initLut();
  pinMode(PA_EN, OUTPUT);
  digitalWrite(PA_EN, LOW);
  Wire.begin(CODEC_SDA, CODEC_SCL, 400000);
  bool ok = i2sInit();                       // MCLK must run before codec setup
  delay(10);
  if (!codec.begin(Wire, CODEC_ADDR)) log_e("ES8311 not found");
  setVolume(settings.volume);
  digitalWrite(PA_EN, HIGH);
  q = xQueueCreate(4, sizeof(Cmd));
  xTaskCreatePinnedToCore(task, "audio", 6144, nullptr, 5, nullptr, 0);
  return ok && codec.ok();
}

bool codecOk() { return codec.ok(); }

static void send(const Cmd &c) {
  if (!q) return;
  if (xQueueSend(q, &c, 0) != pdTRUE) {   // queue full: drop oldest, keep newest
    Cmd junk;
    xQueueReceive(q, &junk, 0);
    xQueueSend(q, &c, 0);
  }
}

bool playFile(const String &path) {
  if (path.isEmpty()) return false;
  Cmd c = {};
  c.type = CMD_FILE;
  strlcpy(c.path, path.c_str(), sizeof(c.path));
  send(c);
  return true;
}

void playFx(SynthFx fx) {
  Cmd c = {};
  c.type = CMD_FX;
  c.fx = fx;
  send(c);
}

void stop() {
  Cmd c = {};
  c.type = CMD_STOP;
  send(c);
}

bool isPlaying() { return playing.load(); }
float level() { return lvl.load(); }

void setVolume(int vol) {
  codec.setVolumeDb(volumeToDb(vol));
  codec.mute(vol <= 0);
}

void powerDown() {
  stop();
  delay(30);
  digitalWrite(PA_EN, LOW);
  codec.standby();
}

void powerUp() {
  codec.start();
  setVolume(settings.volume);
  digitalWrite(PA_EN, HIGH);
}

}  // namespace Audio
