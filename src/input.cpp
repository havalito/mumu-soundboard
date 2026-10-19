#include "input.h"
#include "config.h"
#include <Wire.h>
#include "esp_sleep.h"
#include "driver/gpio.h"

namespace Input {

static QueueHandle_t q;
static volatile bool state[NUM_KEYS];
static volatile uint32_t downAt[NUM_KEYS];
static bool hasTouch = false;
static const uint8_t TP_ADDR = 0x15;   // CST816

static bool tpRead(uint8_t reg, uint8_t *buf, int n) {
  Wire1.beginTransmission(TP_ADDR);
  Wire1.write(reg);
  if (Wire1.endTransmission(false) != 0) return false;
  if (Wire1.requestFrom((int)TP_ADDR, n) != n) return false;
  for (int i = 0; i < n; i++) buf[i] = Wire1.read();
  return true;
}

static void touchInit() {
  pinMode(TP_RST, OUTPUT);
  digitalWrite(TP_RST, LOW);
  delay(10);
  digitalWrite(TP_RST, HIGH);
  delay(60);
  pinMode(TP_INT, INPUT_PULLUP);
  Wire1.begin(TP_SDA, TP_SCL, 400000);
  Wire1.beginTransmission(TP_ADDR);
  hasTouch = (Wire1.endTransmission() == 0);
  if (hasTouch) {
    Wire1.beginTransmission(TP_ADDR);   // disable auto-sleep so it always answers
    Wire1.write(0xFE);
    Wire1.write(0x01);
    Wire1.endTransmission();
  }
  log_i("touch %s", hasTouch ? "found" : "not present");
}

static void task(void *) {
  uint8_t stable[NUM_KEYS], cnt[NUM_KEYS];
  for (int i = 0; i < NUM_KEYS; i++) { stable[i] = 1; cnt[i] = 0; }
  bool touching = false;
  int16_t lastX = 0, lastY = 0;
  uint32_t lastTp = 0;

  for (;;) {
    uint32_t now = millis();
    for (int i = 0; i < NUM_KEYS; i++) {
      uint8_t raw = digitalRead(KEY_PINS[i]);
      if (raw != stable[i]) {
        if (++cnt[i] >= 4) {             // 20 ms stable = real change
          stable[i] = raw;
          cnt[i] = 0;
          InputEvent e = {};
          e.key = i;
          if (raw == LOW) { state[i] = true; downAt[i] = now; e.type = IN_KEY_DOWN; }
          else            { state[i] = false; e.type = IN_KEY_UP; }
          xQueueSend(q, &e, 0);
        }
      } else {
        cnt[i] = 0;
      }
    }

    if (hasTouch && now - lastTp >= 20) {
      lastTp = now;
      uint8_t d[6];
      if (tpRead(0x01, d, 6)) {
        bool finger = (d[1] & 0x0F) > 0;
        InputEvent e = {};
        if (finger) {
          e.x = ((d[2] & 0x0F) << 8) | d[3];
          e.y = ((d[4] & 0x0F) << 8) | d[5];
          lastX = e.x; lastY = e.y;
          e.type = touching ? IN_TOUCH_MOVE : IN_TOUCH;
          xQueueSend(q, &e, 0);
        } else if (touching) {
          e.type = IN_TOUCH_UP;
          e.x = lastX; e.y = lastY;
          xQueueSend(q, &e, 0);
        }
        touching = finger;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void begin() {
  for (int i = 0; i < NUM_KEYS; i++) pinMode(KEY_PINS[i], INPUT_PULLUP);
  touchInit();
  q = xQueueCreate(32, sizeof(InputEvent));
  xTaskCreatePinnedToCore(task, "input", 3072, nullptr, 6, nullptr, 1);
}

bool poll(InputEvent &e) { return xQueueReceive(q, &e, 0) == pdTRUE; }
bool held(int k) { return state[k]; }
uint32_t heldMs(int k) { return state[k] ? millis() - downAt[k] : 0; }
bool anyHeld() { for (int i = 0; i < NUM_KEYS; i++) if (state[i]) return true; return false; }
bool touchPresent() { return hasTouch; }

void enableWakeup() {
  for (int i = 0; i < NUM_KEYS; i++) gpio_wakeup_enable((gpio_num_t)KEY_PINS[i], GPIO_INTR_LOW_LEVEL);
  esp_sleep_enable_gpio_wakeup();
}

}  // namespace Input
