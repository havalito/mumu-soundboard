#pragma once
// ============================================================================
//  Monster Soundboard — board pins and tunables
//  Pins from the Spotpear ESP32-1.54inch-AI V2 schematic + their wiki.
// ============================================================================
#include <Arduino.h>

#define FW_VERSION "1.2.0"
#define MONSTER_NAME "MUMU"

// ---- The five Cherry MX keys (wired from each pad to GND) -----------------
// Order = left-to-right along the smile arc under the screen.
// GPIO0 is also the BOOT pin: don't hold key 5 while switching the toy on,
// or the chip starts in download mode (just switch off/on again).
static const uint8_t KEY_PINS[5] = {39, 40, 43, 44, 0};
#define NUM_KEYS 5

// ---- Display: ST7789V 240x240 on SPI ----------------------------------------
#define LCD_SCLK   4
#define LCD_MOSI   2
#define LCD_CS     5
#define LCD_DC     47
#define LCD_RST    38
#define LCD_BL     42      // backlight, driven through a P-FET: LOW = on
#define LCD_BL_ACTIVE_LOW 1
#define LCD_ROTATION 0     // 0..3 — change if the face is sideways in your case
#define LCD_SPI_HZ 40000000

// ---- Touch (only on the touch version; auto-detected) -----------------------
#define TP_SDA 11
#define TP_SCL 7
#define TP_INT 12
#define TP_RST 6

// ---- Audio: ES8311 codec + NS4150B speaker amp ------------------------------
#define CODEC_SDA  15
#define CODEC_SCL  14
#define CODEC_ADDR 0x18
#define I2S_MCLK   16
#define I2S_BCLK   9
#define I2S_LRCK   45
#define I2S_DOUT   8       // ESP32 -> codec DSDIN
#define I2S_DIN    10      // codec -> ESP32 (mic, unused)
#define PA_EN      46      // amp enable, HIGH = on

// ---- SD card (SDMMC, 1-bit) ------------------------------------------------
#define SD_CLK 17
#define SD_CMD 18
#define SD_D0  21
#define SD_D3  13          // held high for 1-bit mode

// ---- Misc ------------------------------------------------------------------
#define BAT_ADC   1        // VBAT through 100k/100k divider
#define USB_SENSE 41       // USB 5V through 2k/3k divider (high = plugged in)
#define RGB_LED   48       // onboard WS2812

// ---- Behaviour defaults (most are changeable from the web page) -------------
#define DEFAULT_VOLUME      6     // 0..10
#define DEFAULT_MAX_DB     -6     // dB at volume 10. Kid-safe cap.
#define DEFAULT_SLEEP_SEC   90    // idle -> sleepy face, dimmed
#define DEFAULT_OFF_SEC     600   // idle -> screen off, low-power sleep
#define DEFAULT_COLOR       0     // body colour preset index
#define DEFAULT_AP_PASS     "monster123"
#define DEFAULT_IDLE_SEC    40    // sings/plays an idle sound after this long (0 = never)

// Press gestures (only used on keys that have double/triple/hold sounds)
#define MULTI_PRESS_MS 320        // max gap between presses of a double/triple press
#define HOLD_MS        650        // press-and-hold threshold

#define LOW_BATT_MV   3450
#define EMPTY_BATT_MV 3300

// Hold key 1 (leftmost) while switching on, or hold keys 1+5 for 4 s while
// running, to enter Wi-Fi setup mode.
#define CONFIG_HOLD_MS 4000
