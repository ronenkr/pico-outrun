# EspRacer side of the port

EspRacer (the console's launcher and board layer) is a separate project and not part of this repository.
These are the pieces of it that this port needs.

## 1. Register the game

Add one line to EspRacer's `tools/slots.txt` (id, flash slot, project directory relative to EspRacer, build directory):

```
outrun    game2  ../pico-outrun/esp32  build-racer
```

Then `tools/build_all.sh outrun` and `tools/flash_all.sh /dev/ttyACM0 outrun`.

## 2. Menu entry

`game.json` here goes to `/sdcard/games/outrun/game.json` on the card.
`icon.raw` (96 x 96 RGB565, 18 432 bytes) is **not** included: OutRun's cover art belongs to SEGA and is not
for publishing. Make your own with EspRacer's `tools/mkicon.py art.jpeg icon.raw preview.png --crop X,Y,W,H`
and put it next to `game.json`. Without one the launcher shows a coloured letter tile.

## 3. One addition to EspRacer's board layer

The AXP2101 powers the console off when PWR is held for 10 s, and OutRun uses PWR as the brake, so OutRun
turns that long-press shutdown off while it runs (`board_pwr_longpress_shutdown(false)`, restored on exit).
Add this to `components/racer_board/board.c` (after `pmu_init`) and its declaration to `include/board.h`:

```c
/* 0x22 bit 1 (PWROFF_EN): the AXP2101's "long PWR press powers the board off" (XPowersLib: enable/disableLongPressShutdown;
   bit 0 there picks restart or power-off, left alone).
   With it cleared, holding PWR does nothing to the power; a game may then use PWR as a held button. The
   shipped state is remembered the first time and put back by board_pwr_longpress_shutdown(true). The chip
   keeps the setting across an MCU reset, so a game that clears it should restore it before leaving. */
bool board_pwr_longpress_shutdown(bool enable)
{
    static uint8_t shipped;
    static bool have;
    uint8_t v;
    if (!g_pmu || !reg_read(g_pmu, 0x22, &v, 1))
        return false;
    if (!have) {
        shipped = v;
        have = true;
    }
    uint8_t n = enable ? (uint8_t)((v & ~0x02) | (shipped & 0x02)) : (uint8_t)(v & ~0x02);
    if (!reg_write(g_pmu, 0x22, n))
        return false;
    uint8_t chk = 0;
    reg_read(g_pmu, 0x22, &chk, 1);
    ESP_LOGI(TAG, "AXP2101: PWR long-press power-off %s (0x22 %02x -> %02x, reads back %02x)",
             enable ? "restored" : "disabled", v, n, chk);
    return chk == n;
}
/* 0x22 bit 1: the PWR key's long press powers the board off (AXP2101 register map as in
   XPowersLib: PWROFF_EN = 0x22, enableLongPressShutdown sets bit 1, disable clears it) */
bool board_pwr_longpress_off(bool enable)
{
    uint8_t v, w;
    if (!g_pmu || !reg_read(g_pmu, 0x22, &v, 1))
        return false;
    w = enable ? (uint8_t)(v | 0x02) : (uint8_t)(v & ~0x02);
    if (w != v && !reg_write(g_pmu, 0x22, w))
        return false;
    if (!reg_read(g_pmu, 0x22, &w, 1) || ((w >> 1) & 1) != (enable ? 1 : 0)) {
        ESP_LOGW(TAG, "AXP2101: long-press power-off did not change (0x22 %02x)", w);
        return false;
    }
    ESP_LOGI(TAG, "AXP2101: PWR long-press power-off %s (0x22 %02x -> %02x)", enable ? "on" : "OFF", v, w);
    return true;
}
```

```c
/* board.h, next to board_pwr_down() */
bool board_pwr_longpress_shutdown(bool enable);
```

The register is the AXP2101's 0x22, bit 1 (PWROFF_EN), as in XPowersLib's `enable/disableLongPressShutdown`.
The chip keeps the setting across an MCU reset, so a power cycle is what undoes it after a crash.
