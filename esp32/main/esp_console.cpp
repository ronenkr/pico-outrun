/*
 * A serial test console, so the game can be driven and looked at from a PC (the porter's guide, section 8):
 *
 *   S      a screenshot of the last picture:  SHOT w h, a base64 RGB565 line per row, END (tools/racerctl.py shot)
 *   s / L / R  tap the middle / left / right of the screen
 *   a / b  toggle gas / brake held            l / r / c  steer full left / full right / off
 *   A      audio level since the last A:      "AUDIO peak P rms R"
 *
 * Answers are single lines; log lines are interleaved as usual.
 */

#include "esp_console.hpp"

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/usb_serial_jtag.h"
#include "mbedtls/base64.h"

#include "esp_audio.hpp"
#include "esp_input.hpp"
#include "esp_screen.hpp"

static void out(const char *s)
{
    usb_serial_jtag_write_bytes(s, strlen(s), portMAX_DELAY);
}

static void shot(void)
{
    const uint16_t *fb = screen_last();
    unsigned char o[4 * SCR_W + 8];
    char head[32];
    snprintf(head, sizeof head, "\nSHOT %d %d\n", SCR_W, SCR_H);
    out(head);
    for (int y = 0; y < SCR_H; y++)
    {
        size_t olen = 0;
        mbedtls_base64_encode(o, sizeof o - 1, &olen, (const unsigned char *)(fb + y * SCR_W), SCR_W * 2);
        o[olen++] = '\n';
        usb_serial_jtag_write_bytes(o, olen, portMAX_DELAY);
    }
    out("END\n");
}

static void console_task(void *)
{
    for (;;)
    {
        unsigned char c;
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1)
        {
            continue;
        }
        switch (c)
        {
        case 'S': shot(); break;
        case 's': g_inj_tap_x = SCR_W / 2; g_inj_tap = 12; break;  /* tap in the middle */
        case 'L': g_inj_tap_x = 30; g_inj_tap = 12; break;         /* tap on the left */
        case 'R': g_inj_tap_x = SCR_W - 30; g_inj_tap = 12; break; /* tap on the right */
        case 'a': g_inj_gas = !g_inj_gas; break;
        case 'b': g_inj_brake = !g_inj_brake; break;
        case 'l': g_inj_steer = -100; break;
        case 'r': g_inj_steer = 100; break;
        case 'c': g_inj_steer = 0; break;
        case 'A':
        {
            int peak, rms;
            char l[48];
            esp_audio_level(&peak, &rms);
            snprintf(l, sizeof l, "\nAUDIO peak %d rms %d\n", peak, rms);
            out(l);
            break;
        }
        default: break;
        }
    }
}

void esp_console_start(void)
{
    usb_serial_jtag_driver_config_t uc = {};
    uc.tx_buffer_size = 4096;
    uc.rx_buffer_size = 256;
    usb_serial_jtag_driver_install(&uc);
    xTaskCreatePinnedToCore(console_task, "console", 4096, NULL, 2, NULL, 1);
}
