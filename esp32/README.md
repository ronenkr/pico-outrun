# pico-outrun on the EspRacer console (ESP32-S3)

OutRun as a game of the [EspRacer](../../EspRacer) console (Waveshare ESP32-S3-Touch-LCD-1.83). The
Cannonball engine and `port/` data code are the Pico build's own, referenced in place; only `esp32/main/`
is specific to this board. It follows EspRacer's `Readme_porter.md`.

## Build and flash

```bash
cd ../../EspRacer
tools/build_all.sh outrun                  # idf.py -C pico-outrun/esp32 -B build-racer
tools/flash_all.sh /dev/ttyACM0 outrun     # only slot game2; nothing else changes
```

`outrun` is registered in EspRacer's `tools/slots.txt`; its menu entry (`game.json`, `icon.raw`) is in
`EspRacer/sdcard_template/games/outrun/`.

## Game data (not included)

Put either of these in `/sdcard/games/outrun/data/`:

1. `outrun-data.bin`, made on a PC from the MAME `outrun` (revision B parent) ROMs:
   `hosttest/build.sh packer && tools/mkoutrundata <romdir> -o outrun-data.bin`.
   Read into PSRAM in ~1.2 s and CRC-checked.
2. The 31 raw ROM files (also accepted in `data/outrun/`). They are CRC-checked and decoded in PSRAM
   (a few seconds, with a progress bar), then `outrun-data.bin` is written back so the next start is fast.

## Controls

| Input | In a race | Music selection | Title, attract, game over |
|---|---|---|---|
| Tilt | steer (proportional; centred where the console was held at start) | off | - |
| BOOT | accelerate (hold) | previous station | - |
| PWR | brake (tap) | next station | - |
| Touch, left | LOW gear | previous station (left third) | Start |
| Touch, right | HIGH gear | next station (right third) | Start |
| Touch, middle | (gear, by side) | select, start the race | Start |
| BOOT + PWR for 2 s | back to the EspRacer menu | | |

PWR is the brake and not the gas because the console's power chip switches off after a 10 s hold of PWR;
OutRun turns that long-press shutdown off while it runs (`board_pwr_longpress_shutdown`) and restores it when
you leave with BOOT + PWR.

Test console on the USB serial port: `S` screenshot, `s` / `L` / `R` tap the middle / left / right, `a` / `b`
toggle gas / brake, `l` / `r` / `c` steer left / right / off, `A` audio level.

## Layout

| File | Role (Pico equivalent) |
|---|---|
| `main/main.cpp` | boot, data loading, error screens, 60 Hz clock-paced loop (`main.cpp`) |
| `main/esp_glue.cpp` | engine boot and per-slot tick (`port/glue.cpp`) |
| `main/esp_render.cpp`, `sdl2/rendersurface.hpp` | RGB565 palette pass, 320 to 284 column squeeze (`port/render.cpp`) |
| `main/esp_screen.cpp` | three PSRAM pictures, one-rectangle DMA, text screen |
| `main/esp_audio.cpp` | YM2151 + SegaPCM to a mono ring for the codec (`port/audio.cpp`) |
| `main/esp_data.cpp` | data image from the card (`port/outrun_sdload.cpp`) |
| `main/esp_input.cpp` | tilt, buttons, touch (`port/input.cpp` fed by pads) |

## Measured (first run, no optimisation yet)

Engine logic 30 ticks/s (its native rate), about 29 pictures/s drawn, audio 22 050 Hz with no drops,
~240 KB internal RAM and ~5.0 MB PSRAM free while running, image 685 KB of the 2 MB slot.
