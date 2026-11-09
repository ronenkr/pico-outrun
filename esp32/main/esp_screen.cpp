#include "esp_screen.hpp"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_font8x16.h"

static const char *TAG = "screen";

#define NBUF 3
static uint16_t *s_buf[NBUF];
static int s_cur;

void screen_init(void)
{
    for (int i = 0; i < NBUF; i++)
    {
        /* 64-byte aligned PSRAM goes to the LCD by DMA as it is (porter's guide, 2.1) */
        s_buf[i] = (uint16_t *)heap_caps_aligned_calloc(64, SCR_W * SCR_H, sizeof(uint16_t),
                                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_buf[i])
        {
            ESP_LOGE(TAG, "no PSRAM for picture %d", i);
            abort();
        }
    }
    s_cur = 0;
}

uint16_t *screen_back(void)
{
    return s_buf[s_cur];
}

const uint16_t *screen_last(void)
{
    return s_buf[(s_cur + NBUF - 1) % NBUF];
}

void screen_present(void)
{
    /* one rectangle for the whole picture (not strips: esp_lcd waits for its queue between rectangles) */
    board_lcd_draw(0, 0, SCR_W, SCR_H, s_buf[s_cur]);
    s_cur = (s_cur + 1) % NBUF;
}

void screen_begin(void)
{
    /* at most the previous picture is still queued, so the one we are about to draw into (sent two
     * pictures ago) is out (porter's guide, 6.3) */
    board_lcd_wait_pending(1);
}

void screen_blank_all(void)
{
    board_lcd_wait();
    for (int i = 0; i < NBUF; i++)
    {
        memset(s_buf[i], 0, SCR_W * SCR_H * sizeof(uint16_t));
    }
}

void screen_clear(uint16_t bg)
{
    screen_begin();
    uint16_t *fb = screen_back();
    for (int i = 0; i < SCR_W * SCR_H; i++)
    {
        fb[i] = bg;
    }
}

void screen_text(int col, int row, const char *s, uint16_t fg, uint16_t bg)
{
    if (row < 0 || row >= TEXT_ROWS)
    {
        return;
    }
    const int len = (int)strlen(s);
    if (col < 0)
    {
        col = (TEXT_COLS - len) / 2;
        if (col < 0)
        {
            col = 0;
        }
    }
    uint16_t *fb = screen_back();
    for (int i = 0; i < len && col + i < TEXT_COLS; i++)
    {
        int ch = (unsigned char)s[i];
        if (ch < 0x20 || ch > 0x7E)
        {
            ch = '?';
        }
        const uint8_t *glyph = esp_font8x16[ch - 0x20];
        for (int y = 0; y < 16; y++)
        {
            uint16_t *dst = fb + (row * 16 + y) * SCR_W + (col + i) * 8;
            const uint8_t bits = glyph[y];
            for (int x = 0; x < 8; x++)
            {
                dst[x] = (bits & (0x80 >> x)) ? fg : bg;
            }
        }
    }
}

void screen_bar(int row, int percent, uint16_t fg, uint16_t bg)
{
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    const int x0 = 8, w = SCR_W - 16;
    const int filled = w * percent / 100;
    uint16_t *fb = screen_back();
    for (int y = row * 16 + 3; y < row * 16 + 13; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const bool edge = (y == row * 16 + 3) || (y == row * 16 + 12) || x == 0 || x == w - 1;
            fb[y * SCR_W + x0 + x] = (edge || x < filled) ? fg : bg;
        }
    }
}
