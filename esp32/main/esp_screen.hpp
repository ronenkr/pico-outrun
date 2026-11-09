/*
 * The LCD side of the port: three PSRAM pictures of LCD_W x LCD_H (RGB565, 64-byte aligned, as
 * board_lcd_draw wants them), the one the game is drawing into, and a small text screen for what has to
 * be said before the engine exists ("building the game data", "no ROMs found").
 *
 * The text screen is 35 columns x 15 rows of an 8x16 font.
 */

#pragma once

#include <stdint.h>

#include "esp_board.hpp"

#define SCR_W LCD_W
#define SCR_H LCD_H

/* OutRun is 320x224; it is squeezed to SCR_W columns and centred vertically */
#define OUTRUN_W 320
#define OUTRUN_H 224
#define OUTRUN_YOFFSET ((SCR_H - OUTRUN_H) / 2)

#define TEXT_COLS (SCR_W / 8)
#define TEXT_ROWS (SCR_H / 16)

#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

void screen_init(void);

/* call before drawing a new picture: waits until the LCD is done with the buffer about to be reused */
void screen_begin(void);
/* the picture to draw into now */
uint16_t *screen_back(void);
/* the picture most recently handed to the LCD (for the serial screenshot) */
const uint16_t *screen_last(void);
/* send it to the LCD (in the background) and move on to the next buffer */
void screen_present(void);
/* all three pictures black, so a leftover text screen cannot show in the letterbox */
void screen_blank_all(void);

/* text screen, drawn into the back picture; col < 0 centres the string */
void screen_clear(uint16_t bg);
void screen_text(int col, int row, const char *s, uint16_t fg, uint16_t bg);
void screen_bar(int row, int percent, uint16_t fg, uint16_t bg);
