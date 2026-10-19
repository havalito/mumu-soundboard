// ============================================================================
//  Monster Soundboard — main firmware
//  Spotpear ESP32-S3 AI 1.54" board + 5 keys + salvaged JBL driver
// ============================================================================
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "esp_sleep.h"
#include "config.h"
#include "settings.h"
#include "audio.h"
#include "sounds.h"
#include "input.h"
#include "face.h"
#include "webui.h"

// ---------------------------------------------------------------- display --
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI _bus;
public:
  LGFX() {
    auto b = _bus.config();
    b.spi_host = SPI2_HOST;
    b.spi_mode = 0;
    b.freq_write = LCD_SPI_HZ;
    b.freq_read = 16000000;
    b.spi_3wire = true;
    b.use_lock = true;
    b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = LCD_SCLK;
    b.pin_mosi = LCD_MOSI;
    b.pin_miso = -1;
    b.pin_dc = LCD_DC;
    _bus.config(b);
    _panel.setBus(&_bus);

    auto p = _panel.config();
    p.pin_cs = LCD_CS;
    p.pin_rst = LCD_RST;
    p.pin_busy = -1;
    p.panel_width = 240;
    p.panel_height = 240;
    p.memory_width = 240;
    p.memory_height = 320;
    p.offset_x = 0;
    p.offset_y = 0;
    p.offset_rotation = 0;
    p.readable = false;
    p.invert = true;
    p.rgb_order = false;
    p.dlen_16bit = false;
    p.bus_shared = false;
    _panel.config(p);
    setPanel(&_panel);
  }
};

static LGFX lcd;
static LGFX_Sprite canvas(&lcd);
static Face face;

// ------------------------------------------------------------------ state --
enum Power { P_AWAKE, P_DIM, P_SLEEP };
static Power power = P_AWAKE;
static uint32_t lastActivity = 0;
static bool configMode = false;
static bool goodnightDone = false;
static int battMv = 4000;
static bool charging = false;
static uint32_t battShowUntil = 0;
static uint32_t pressTimes[10];
static int pressIdx = 0;
static uint32_t lastMash = 0;
static uint32_t lastVolStep = 0;
static float ledLevel = 0;
static uint8_t ledRGB[3] = {0, 0, 0};

static const SynthFx KEY_FX[NUM_KEYS] = {FX_GIGGLE, FX_SLIDE_UP, FX_RASPBERRY, FX_LOVE, FX_DIZZY};
static const uint8_t KEY_LED[NUM_KEYS][3] = {{255, 200, 0}, {0, 180, 255}, {120, 255, 0}, {255, 40, 120}, {180, 60, 255}};

// -------------------------------------------------------------- backlight --
static int blNow = -1;
static void backlight(int pct) {
  pct = constrain(pct, 0, 100);
  if (pct == blNow) return;
  blNow = pct;
  int duty = pct * 255 / 100;
#if LCD_BL_ACTIVE_LOW
  duty = 255 - duty;
#endif
  ledcWrite(LCD_BL, duty);
}

static void fadeBacklight(int to, int ms) {
  int from = max(blNow, 0);
  for (int i = 1; i <= 20; i++) { backlight(from + (to - from) * i / 20); delay(ms / 20); }
}

static void led(const uint8_t *rgb) {
  memcpy(ledRGB, rgb, 3);
  ledLevel = 1.0f;
}

static void ledUpdate(float dt) {
  if (ledLevel <= 0) return;
  ledLevel -= dt * 2.5f;
  float l = max(0.0f, ledLevel) * 0.25f;       // keep it gentle
  rgbLedWrite(RGB_LED, ledRGB[0] * l, ledRGB[1] * l, ledRGB[2] * l);
}

// ---------------------------------------------------------------- battery --
static void readBattery(bool force = false) {
  static uint32_t last = 0;
  static float ema = 0;
  if (!force && millis() - last < 2000) return;
  if (Audio::isPlaying() && !force) return;          // voltage sags while playing
  last = millis();
  int mv = analogReadMilliVolts(BAT_ADC) * 2;
  ema = (ema == 0) ? mv : ema * 0.8f + mv * 0.2f;
  battMv = (int)ema;
  bool ch = digitalRead(USB_SENSE) == HIGH;
  if (ch != charging) battShowUntil = millis() + 4000;
  charging = ch;
}

// ------------------------------------------------------------------ power --
static void markActive() { lastActivity = millis(); goodnightDone = false; }

static void wakeUp() {
  if (power == P_AWAKE) return;
  power = P_AWAKE;
  backlight(settings.brightness);
  face.setMode(FM_AWAKE);
  battShowUntil = millis() + 3000;
  markActive();
}

// Screen off + light sleep until a key is pressed. Wakes in a few ms.
static void lightSleepUntilKey() {
  fadeBacklight(0, 300);
  Audio::powerDown();
  lcd.sleep();
  rgbLedWrite(RGB_LED, 0, 0, 0);
  // PWM stops in light sleep, so park the backlight pin as a plain GPIO (off)
  ledcDetach(LCD_BL);
  pinMode(LCD_BL, OUTPUT);
  digitalWrite(LCD_BL, LCD_BL_ACTIVE_LOW ? HIGH : LOW);
  Input::enableWakeup();
  bool usbAtSleep = digitalRead(USB_SENSE) == HIGH;
  for (;;) {
    while (Input::anyHeld()) delay(20);           // wait for release before sleeping
    esp_sleep_enable_timer_wakeup(60ULL * 1000000ULL);
    esp_light_sleep_start();
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) break;
    bool usb = digitalRead(USB_SENSE) == HIGH;    // plugged in/out: show the badge
    if (usb != usbAtSleep) break;
  }
  ledcAttach(LCD_BL, 5000, 8);
  blNow = -1;
  backlight(0);
  lcd.wakeup();
  Audio::powerUp();
  InputEvent e;
  delay(40);
  while (Input::poll(e)) {}                       // the wake press only wakes
  power = P_SLEEP;
  wakeUp();
}

static void shutdownEmpty() {
  face.setMode(FM_EMPTY);
  for (int i = 0; i < 90; i++) {                   // show "charge me" for 3 s
    face.update(0.033f, 0, false);
    face.render();
    canvas.pushSprite(0, 0);
    delay(33);
  }
  fadeBacklight(0, 300);
  Audio::powerDown();
  lcd.sleep();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);     // key 5 wakes it
  esp_deep_sleep_start();
}

// ------------------------------------------------------------------ input --
static void enterConfig() {
  if (configMode) return;
  configMode = true;
  Audio::playFx(FX_BLIP);
  WebUI::begin();
  face.setMode(FM_CONFIG);
}

static void onSettingsChanged() {
  Audio::setVolume(settings.volume);
  face.setColor(settings.color);
  backlight(settings.brightness);
}

// Play a slot: a random/next WAV with its chosen animation, or the key's
// built-in effect if the slot is empty.
static bool userSound = false;   // a sound someone asked for is playing (keeps MUMU awake)

static void playSlot(int slot, int key) {
  String path;
  Reaction anim;
  if (Sounds::next(slot, path, anim)) {
    Audio::playFile(path);
  } else {
    anim = Sounds::defaultReaction(slot);
    if (slot == SLOT_EYE_L || slot == SLOT_EYE_R) Audio::playFx(FX_POP);
    else Audio::playFx(key >= 0 ? KEY_FX[key] : FX_SONG_0);
  }
  if (slot != SLOT_IDLE) userSound = true;
  face.react(anim, key);
  if (key >= 0) led(KEY_LED[key]);
}

// ---- Touch: poke an eye (or anywhere), swipe up/down for volume ---------------
static const int SWIPE_STEP_PX = 28;   // finger travel per volume step
static int16_t tDownX, tDownY, tLastStepY;
static bool tSwiping = false, tIgnore = false;

static void changeVolume(int dir) {
  int v = constrain(settings.volume + dir, 0, 10);
  if (v != settings.volume) { settings.volume = v; settingsSave(); }
  Audio::setVolume(settings.volume);
  Audio::playFx(FX_BLIP);
  face.showVolume(settings.volume);
  markActive();
}

static void touchEvent(const InputEvent &e) {
  if (e.type == IN_TOUCH) {
    bool wasAsleep = (power == P_SLEEP);
    wakeUp();
    markActive();
    tIgnore = wasAsleep || configMode;          // the touch that wakes it only wakes it
    tDownX = e.x; tDownY = tLastStepY = e.y;
    tSwiping = false;
    return;
  }
  if (tIgnore) return;
  if (e.type == IN_TOUCH_MOVE) {
    int dy = e.y - tLastStepY, dx = e.x - tDownX;
    if (!tSwiping && abs(e.y - tDownY) > 22 && abs(e.y - tDownY) > abs(dx)) {
      tSwiping = true;
      face.showVolume(settings.volume);
    }
    if (tSwiping && abs(dy) >= SWIPE_STEP_PX) {  // up = louder, down = quieter
      changeVolume(dy < 0 ? +1 : -1);
      tLastStepY = e.y;
    }
    if (tSwiping) face.showVolume(settings.volume);   // keep the bar up while swiping
    return;
  }
  if (e.type == IN_TOUCH_UP && !tSwiping) {     // a tap = poke
    int eye = Face::eyeAt(tDownX, tDownY);
    if (eye >= 0) {
      playSlot(eye == 0 ? SLOT_EYE_L : SLOT_EYE_R, -1);   // sound + its chosen animation
      if (face.reaction() == R_POKE) face.poke(tDownX, tDownY);   // default: wink the poked eye
    } else {
      face.poke(tDownX, tDownY);
      Audio::playFx(FX_POP);
    }
  }
}

// ---- Gestures: single / double / triple / hold ------------------------------
// A key with only single-press sounds plays instantly on key-down.
// Only keys that have double/triple/hold sounds wait to see what you're doing.
struct KeyState {
  uint8_t count;       // presses in the current gesture
  bool down;
  bool consumed;       // hold fired or a combo used this press: ignore the release
  uint32_t lastUp;
};
static KeyState ks[NUM_KEYS];

struct KeyCaps { bool dbl, tpl, hold; int maxMulti; };
static KeyCaps caps(int k) {
  KeyCaps c;
  c.dbl = Sounds::count(Sounds::slot(k, G_DOUBLE)) > 0;
  c.tpl = Sounds::count(Sounds::slot(k, G_TRIPLE)) > 0;
  c.hold = Sounds::count(Sounds::slot(k, G_HOLD)) > 0;
  c.maxMulti = c.tpl ? 3 : (c.dbl ? 2 : 1);
  return c;
}

static Gesture gestureFor(int count, const KeyCaps &c) {
  if (count >= 3 && c.tpl) return G_TRIPLE;
  if (count >= 2 && c.dbl) return G_DOUBLE;
  return G_PRESS;
}

static void fireGesture(int k, Gesture g) {
  ks[k].count = 0;
  log_i("key %d gesture %d", k + 1, g);
  playSlot(Sounds::slot(k, g), k);
}

static void resetGestures() {
  for (int i = 0; i < NUM_KEYS; i++) { ks[i].count = 0; ks[i].consumed = ks[i].down; }
}

static void keyDown(int k) {
  uint32_t now = millis();
  bool wasAsleep = (power == P_SLEEP);
  wakeUp();
  ks[k].down = true;
  if (wasAsleep) { ks[k].consumed = true; return; }   // first press just wakes it up
  face.glance(k);

  // Mash detection: 8 presses inside 2 seconds
  if (!configMode) {
    pressTimes[pressIdx] = now;
    pressIdx = (pressIdx + 1) % 10;
    int recent = 0;
    for (int i = 0; i < 10; i++) if (now - pressTimes[i] < 2000) recent++;
    if (recent >= 8 && now - lastMash > 5000) {
      lastMash = now;
      resetGestures();
      face.react(R_MASH);
      String p = Sounds::special("dizzy");
      if (p.length()) Audio::playFile(p); else Audio::playFx(FX_DIZZY);
      return;
    }
    if (now - lastMash < 2000 && face.busy()) { ks[k].consumed = true; return; }  // let the WHOA finish
  }

  KeyCaps c = caps(k);
  ks[k].consumed = false;
  ks[k].count++;
  if (c.maxMulti == 1 && !c.hold) { fireGesture(k, G_PRESS); return; }        // instant
  if (c.maxMulti > 1 && ks[k].count >= c.maxMulti) { fireGesture(k, gestureFor(ks[k].count, c)); ks[k].consumed = true; }
}

static void keyUp(int k) {
  ks[k].down = false;
  if (ks[k].consumed) { ks[k].consumed = false; ks[k].count = 0; return; }
  if (ks[k].count > 0) ks[k].lastUp = millis();
}

static void gestureTick() {
  uint32_t now = millis();
  for (int k = 0; k < NUM_KEYS; k++) {
    KeyState &s = ks[k];
    if (s.count == 0) continue;
    KeyCaps c = caps(k);
    if (s.down) {
      bool otherHeld = false;
      for (int j = 0; j < NUM_KEYS; j++) if (j != k && Input::held(j)) otherHeld = true;
      if (c.hold && s.count == 1 && !otherHeld && Input::heldMs(k) >= HOLD_MS) {
        fireGesture(k, G_HOLD);
        s.consumed = true;
      }
    } else if (now - s.lastUp > (uint32_t)(c.maxMulti > 1 ? MULTI_PRESS_MS : 0)) {
      fireGesture(k, gestureFor(s.count, c));
    }
  }
}

static void checkCombos() {
  uint32_t now = millis();
  // Keys 1+5 held: setup mode
  if (Input::heldMs(0) > CONFIG_HOLD_MS && Input::heldMs(4) > CONFIG_HOLD_MS) {
    ks[0].consumed = ks[4].consumed = true;
    enterConfig();
  }

  // Keys 1+2 held 1.5 s: volume down; keys 4+5: volume up (repeats while held)
  int dir = 0;
  if (Input::heldMs(0) > 1500 && Input::heldMs(1) > 1500 && !Input::held(4)) { dir = -1; ks[0].consumed = ks[1].consumed = true; }
  if (Input::heldMs(3) > 1500 && Input::heldMs(4) > 1500 && !Input::held(0)) { dir = +1; ks[3].consumed = ks[4].consumed = true; }
  if (dir && now - lastVolStep > 600) {
    lastVolStep = now;
    int v = constrain(settings.volume + dir, 0, 10);
    if (v != settings.volume) { settings.volume = v; settingsSave(); }
    Audio::setVolume(settings.volume);
    Audio::playFx(FX_BLIP);
    face.showVolume(settings.volume);
    markActive();
  }
}

// ---- Idle singing -----------------------------------------------------------
static uint32_t lastIdleSound = 0;

static void idleSing() {
  int files = Sounds::count(SLOT_IDLE);
  int pool = files + (settings.songs ? NUM_SONGS : 0);
  if (pool == 0) return;
  int pick = random(pool);
  if (pick < files) {
    playSlot(SLOT_IDLE, -1);
  } else {
    Audio::playFx((SynthFx)(FX_SONG_0 + (pick - files)));
    face.react(R_SING);
  }
}

// ------------------------------------------------------------------ setup --
void setup() {
  Serial.begin(115200);
  log_w("boot, reset reason %d", (int)esp_reset_reason());   // 9 = brownout, 6/7 = watchdog
  settingsLoad();

  ledcAttach(LCD_BL, 5000, 8);
  backlight(0);
  pinMode(USB_SENSE, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC, ADC_11db);
  rgbLedWrite(RGB_LED, 0, 0, 0);

  lcd.init();
  lcd.setRotation(LCD_ROTATION);
  lcd.fillScreen(TFT_BLACK);

  canvas.setColorDepth(16);
  canvas.setPsram(false);
  if (!canvas.createSprite(240, 240)) { canvas.setPsram(true); canvas.createSprite(240, 240); }
  face.begin(&canvas);
  face.setName(MONSTER_NAME);
  face.setColor(settings.color);

  Input::begin();
  bool sdOk = Sounds::begin();
  bool audioOk = Audio::begin();
  log_i("SD %s, audio %s, PSRAM %u", sdOk ? "ok" : "MISSING", audioOk ? "ok" : "FAIL", ESP.getPsramSize());
  randomSeed(esp_random());

  readBattery(true);
  battShowUntil = millis() + 3000;

  // Holding key 1 while switching on -> setup mode
  uint32_t t0 = millis();
  while (digitalRead(KEY_PINS[0]) == LOW && millis() - t0 < 1500) delay(10);
  bool bootConfig = (digitalRead(KEY_PINS[0]) == LOW);

  face.render();
  canvas.pushSprite(0, 0);
  fadeBacklight(settings.brightness, 250);

  if (bootConfig) {
    ks[0].consumed = true;
    enterConfig();
  } else if (battMv < EMPTY_BATT_MV && !charging) {
    shutdownEmpty();
  } else {
    face.react(R_HELLO);
    String hello = Sounds::special("hello");
    if (hello.length()) Audio::playFile(hello); else Audio::playFx(FX_HELLO);
  }
  WebUI::onSettingsChanged(onSettingsChanged);
  WebUI::onPreview([](Reaction r) { face.setMode(FM_AWAKE); face.react(r); });
  markActive();
}

// ------------------------------------------------------------------- loop --
void loop() {
  static uint32_t lastUs = micros();
  uint32_t frameStart = millis();
  uint32_t nowUs = micros();
  float dt = (nowUs - lastUs) / 1e6f;
  lastUs = nowUs;

  InputEvent e;
  while (Input::poll(e)) {
    if (e.type == IN_KEY_DOWN) { keyDown(e.key); markActive(); }
    else if (e.type == IN_KEY_UP) keyUp(e.key);
    else touchEvent(e);
  }
  gestureTick();
  checkCombos();
  if (Audio::isPlaying()) { if (userSound) markActive(); } else userSound = false;

  if (configMode) {
    WebUI::loop();
    // back to the Wi-Fi screen once a preview from the web page has finished
    if (face.mode() == FM_AWAKE && !face.busy() && !Audio::isPlaying()) face.setMode(FM_CONFIG);
    face.setConfigText(WebUI::line(0).c_str(), WebUI::line(1).c_str(), WebUI::line(2).c_str());
    markActive();
  }

  // Battery
  readBattery();
  if (!configMode && power == P_AWAKE) {
    if (battMv < EMPTY_BATT_MV && !charging && !Audio::isPlaying()) shutdownEmpty();
    FaceMode want = (battMv < LOW_BATT_MV && !charging) ? FM_LOWBATT : FM_AWAKE;
    if (face.mode() == FM_AWAKE || face.mode() == FM_LOWBATT) face.setMode(want);
  }
  face.setBattery(battMv, charging, millis() < battShowUntil);

  // Idle singing, then sleepy -> asleep -> off
  uint32_t now = millis();
  uint32_t idle = now - lastActivity;
  bool quiet = !Audio::isPlaying() && !face.busy();
  if (!configMode) {
    if (power == P_AWAKE && quiet && settings.idleSec > 0 && battMv >= LOW_BATT_MV &&
        now - max(lastActivity, lastIdleSound) > (uint32_t)settings.idleSec * 1000 &&
        idle + 8000 < (uint32_t)settings.sleepSec * 1000) {
      lastIdleSound = now;
      idleSing();
    }
    if (power == P_AWAKE && quiet && idle > (uint32_t)settings.sleepSec * 1000) {
      power = P_DIM;
      face.setMode(FM_SLEEPY);
      backlight(max(10, settings.brightness / 3));
    }
    if (power == P_DIM && !goodnightDone && idle > (uint32_t)settings.sleepSec * 1000 + 20000) {
      goodnightDone = true;
      face.react(R_GOODNIGHT);
      String gn = Sounds::special("goodnight");
      if (gn.length()) Audio::playFile(gn); else Audio::playFx(FX_GOODNIGHT);
    }
    if (power == P_DIM && goodnightDone && quiet) {
      power = P_SLEEP;
      face.setMode(FM_SLEEP);
      backlight(max(5, settings.brightness / 6));
    }
    if (settings.offSec > 0 && power == P_SLEEP && idle > (uint32_t)settings.offSec * 1000) {
      lightSleepUntilKey();
      return;
    }
  }

  ledUpdate(dt);
  face.update(dt, Audio::level(), Audio::isPlaying());
  face.render();
  canvas.pushSprite(0, 0);

  // ~30 fps awake, ~12 fps asleep
  uint32_t budget = (power == P_SLEEP) ? 80 : 33;
  uint32_t used = millis() - frameStart;
  if (used < budget) delay(budget - used);
}
