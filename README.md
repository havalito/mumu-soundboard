# MUMU — monster soundboard firmware

Custom firmware for the kids' soundboard built on the **Spotpear ESP32-S3 "AI 1.54" (muma) board**:
five Cherry MX keys, the salvaged JBL driver on the board's speaker connector, and MUMU, an animated
monster face on the 1.54" screen.

![face preview](face-preview.gif)

## What it does

- **20 sounds on 5 keys.** Every key has four gestures — **press, double press, triple press, hold** —
  each with its own sounds. A key that only has press sounds still reacts instantly; it only waits
  (~0.3 s) to tell a single from a double once you give it double/triple/hold sounds. A missing
  double or triple falls back to the single press.
- **14 animations, picked per sound** on the web page: laugh, gasp, silly, love, dizzy, angry, scared,
  sing, cool (sunglasses), sneeze ("ACHOO!"), sad, party (hat + confetti), kiss and toot (green cloud).
  Sounds without a choice use their key's default:

  | Key (left → right) | Default animation | Built-in sound if the key has no files |
  |---|---|---|
  | 1 | laugh | giggle arpeggio |
  | 2 | gasp | slide whistle |
  | 3 | silly | raspberry |
  | 4 | love | sweet swoop |
  | 5 | dizzy | wobbly wah |

- **It sings when idle.** After 40 s without presses it sings a nursery tune in a little "la-la"
  monster voice (Twinkle Twinkle, Frère Jacques / Broder Jakob, Mary Had a Little Lamb, Ode to Joy,
  Old MacDonald) or plays one of your idle sounds, then again a while later, before getting sleepy.
- **It talks along.** The mouth opens and closes with the loudness of whatever is playing.
- **It glances at the key you pressed** the instant it goes down (keys left-to-right in the smile arc).
- **Button mashing** (8 presses in 2 s) triggers an extra-dizzy "WHOA!".
- **Alive when idle:** blinks (sometimes double), looks around, yawns, whistles.
- **Gets sleepy** after 90 s (dims, droopy eyelids), says goodnight and snores "z z", then turns the
  screen off and light-sleeps after 10 min. **Any key wakes it** (that first press only wakes it).
- **Says hi** with a speech bubble when switched on.
- **Touch screen (auto-detected):**
  - **Poke an eye** and MUMU winks it and plays one of that eye's sounds (`/sounds/eye-left`, `/sounds/eye-right`; any number each, animation pickable). Poking elsewhere makes it flinch and giggle.
  - **Swipe up / down** on the face to turn the volume up / down; the volume bar shows while you swipe. The kid-safe maximum still applies.
- **Battery aware:** battery badge on start/charge, tired face below 3.45 V, "charge me!" and deep sleep below 3.3 V.
- **Kid-safe volume:** volume 0–10 is capped by a maximum loudness (default −6 dB) you set on the web page.
- **Colour presets:** slime green, grape, tangerine, sky blue, bubblegum, sunny.

## Controls

| Action | How |
|---|---|
| Play | press, double press, triple press or hold a key |
| Volume down / up | swipe down / up on the screen, or hold **keys 1+2** / **keys 4+5** together for 1.5 s |
| Setup mode (Wi-Fi) | hold **key 1** while switching on, **or** hold **keys 1+5** for 4 s |
| Off | the board's own power button |

## Putting sounds on it

**Easiest — from your phone (setup mode):**
1. Enter setup mode. The screen shows a Wi-Fi name like `MUMU-3F2A`, the password (default `monster123`) and an address.
2. Join that Wi-Fi; the page usually pops up by itself, otherwise open `http://192.168.4.1`.
3. Pick a key and a tab (Press / Double / Triple / Hold) and drop in any audio file (MP3, M4A, WAV, OGG…).
   The page trims silence, levels it, converts it to 22 kHz mono WAV *on your phone*, and uploads it.
4. **Choose an animation** from the dropdown next to each sound. A live MUMU at the top of the page
   plays the sound with that animation, mouth moving in time. ▶ previews on the phone, 📟 plays it on MUMU itself.
5. The *Animations* row lets you try every animation without a sound; *When nobody is playing* holds
   the idle sounds, the sing timer, and buttons to hear the built-in songs on MUMU.
6. Tap **Restart MUMU** when done.

If you add your home Wi-Fi on the page, setup mode joins it instead and you can use `http://mumu.local`.

**Or copy files straight to the SD card** (FAT32):

```
/sounds/1/*.wav          key 1, press (any number of files)
/sounds/1/double/*.wav   key 1, double press
/sounds/1/triple/*.wav   key 1, triple press
/sounds/1/hold/*.wav     key 1, press and hold
...                      same for keys 2-5
/sounds/idle/*.wav       idle sounds (mixed in with the built-in songs)
/sounds/eye-left/*.wav   poking the left eye (as you look at MUMU)
/sounds/eye-right/*.wav  poking the right eye
/sounds/hello.wav        optional, plays at power-on
/sounds/goodnight.wav    optional, plays when it falls asleep
/sounds/dizzy.wav        optional, plays on button mashing
```

Animations chosen on the web page are stored next to the sounds in each folder's `_anim.txt`
(`cat meow.wav=love`), so you can also edit that file by hand.

WAV must be PCM 8- or 16-bit, mono or stereo, 8–48 kHz (22.05 kHz mono 16-bit is ideal). There's no length limit — a whole song works (about 2.6 MB per minute).

## Flashing

**Option A — prebuilt binary (no tools to install):**
Open <https://espressif.github.io/esptool-js/> in Chrome/Edge, plug in the board's USB-C, Connect,
and flash `firmware/monster-soundboard-factory.bin` at address **0x0**.
If it won't connect: hold the BOOT button (key 5 / GPIO0 also works) while switching on.

**Option B — build it yourself (PlatformIO):**
```
pio run -t upload
pio device monitor
```
The project uses the pioarduino ESP32 platform (Arduino core 3.x) and LovyanGFX 1.2.33.

**Updates later:** setup mode → *Firmware update* → choose `monster-soundboard-ota.bin`.

> Want the original AI-chat firmware back? Spotpear's wiki for the board has the factory images.

**Updating from 1.0:** setup mode → *Firmware update* → `monster-soundboard-ota.bin`. Existing sounds and settings are kept.

## Hardware notes

| Function | GPIO |
|---|---|
| Keys 1–5 (to GND, internal pull-ups) | 39, 40, 43, 44, 0 |
| LCD ST7789 240×240: SCLK / MOSI / CS / DC / RST / BL | 4 / 2 / 5 / 47 / 38 / 42 (BL active-low) |
| ES8311 I2C SDA / SCL (addr 0x18) | 15 / 14 |
| I2S MCLK / BCLK / LRCK / DOUT | 16 / 9 / 45 / 8 |
| NS4150B amp enable | 46 |
| SD (SDMMC 1-bit) CLK / CMD / D0 / D3 | 17 / 18 / 21 / 13 |
| Touch CST816 SDA / SCL / INT / RST | 11 / 7 / 12 / 6 |
| Battery ADC (×2 divider) | 1 |
| USB-present sense | 41 |
| Onboard RGB LED | 48 (flashes a colour per key) |

- **GPIO0 is the BOOT pin.** Don't hold key 5 while switching on, or it starts in download mode (just switch off/on).
- **GPIO43/44 are the UART0 pads.** Fine for keys — logging goes over USB instead — but a key held during power-on can garble the boot log.
- **Key 1 at power-on** enters setup mode, so a kid holding it just sees the Wi-Fi screen; switching off/on fixes it.

## Tuning (src/config.h)

- `LCD_ROTATION` — rotate the face if it's sideways in your case (0–3).
- `KEY_PINS` — reorder if your wiring doesn't match left→right.
- `LCD_SPI_HZ` — 40 MHz is safe; 80 MHz gives smoother animation if your panel tolerates it.
- `MULTI_PRESS_MS` / `HOLD_MS` — how fast a double press must be (320 ms) and how long a hold is (650 ms).
- `MONSTER_NAME` — the name in the hello bubble, Wi-Fi name and page title.
- `DEFAULT_*` — first-boot defaults (most are also on the web page).

## First-boot checks (from 1.0)

These were the open questions before the first flash — all fine on the real board. Kept in case you build for a different panel:

1. **Face colours look wrong / negative** → flip `p.invert` in `main.cpp`.
2. **Screen stays dark** → set `LCD_BL_ACTIVE_LOW` to 0.
3. **Face shifted or garbled at the edge** → set `p.offset_y = 80` (some panels need it for rotation 2).
4. **No sound** → watch the serial log for `ES8311 not found`; check the speaker is on the board's connector.
5. **Wrong reaction for a key** → reorder `KEY_PINS`.

## Repository layout

```
firmware/   ready-to-flash binaries (factory = first flash at 0x0, ota = updates via the setup page)
src/        firmware source (PlatformIO, Arduino core 3.x)
web/        setup page source (bundled into the firmware at build time)
keycaps/    MUMU keycaps: STLs, fit tester and CadQuery source
docs/       images
```

## Source layout

```
src/main.cpp       display setup, power/sleep state machine, key handling
src/face.*         MUMU: expressions, the 14 animations, particles, overlays
src/audio.*        I2S + playback task, WAV player, synthesized effects, level meter
src/es8311.*       codec driver
src/input.*        key debouncing, combos, touch
src/sounds.*       SD card and per-key sound lists
src/settings.*     saved settings (NVS)
src/webui.cpp      setup-mode Wi-Fi, API, uploads, OTA
web/index.html     the setup page            } bundled + gzipped into src/webui_html.h
web/face.js        browser port of face.cpp  } by tools/build_web.py on every build
```

When you change an animation in `face.cpp`, make the same change in `web/face.js` so the preview matches.


## License

[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) — free to share and remix for non-commercial use, with credit, under the same license. See `LICENSE`.
