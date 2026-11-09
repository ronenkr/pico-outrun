/*
 * pico-outrun on the EspRacer console: boot, load the game data from the SD card, then run the Cannonball
 * engine paced by the clock.
 *
 *   /sdcard/games/outrun/data/   outrun-data.bin   (tools/mkoutrundata), or the raw MAME 'outrun' ROMs
 */

#include <initializer_list>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_board.hpp"

#include "esp_audio.hpp"
#include "esp_data.hpp"
#include "esp_glue.hpp"
#include "esp_screen.hpp"
#include "esp_console.hpp"

static const char *TAG = "outrun";

#define WHITE RGB565(255, 255, 255)
#define GREY RGB565(150, 150, 150)
#define YELLOW RGB565(255, 220, 0)
#define RED RGB565(255, 60, 60)
#define GREEN RGB565(80, 220, 80)
#define BG RGB565(0, 0, 40)

/* ------------------------------------------------------------------------- */
/* Messages                                                                   */
/* ------------------------------------------------------------------------- */

static void title(const char *sub)
{
    screen_clear(BG);
    screen_text(-1, 1, "O U T R U N", YELLOW, BG);
    screen_text(-1, 2, sub, GREY, BG);
}

static void present_text(void)
{
    screen_present();
}

/* a message that stays: the game cannot start; BOOT + PWR for 2 s returns to the menu */
static void say(const char *head, const char *l1, const char *l2, const char *l3, const char *l4)
{
    title("on EspRacer");
    screen_text(-1, 5, head, RED, BG);
    int row = 7;
    for (const char *l : {l1, l2, l3, l4})
    {
        if (l)
        {
            screen_text(1, row, l, WHITE, BG);
            row++;
        }
    }
    screen_text(-1, TEXT_ROWS - 2, "BOOT + PWR for 2 s: back to the menu", GREY, BG);
    present_text();
}

static void progress_screen(int step, int steps, const char *what)
{
    static int last_pct = -1;
    const int pct = steps ? step * 100 / steps : 0;
    if (pct == last_pct && step != steps)
    {
        return; /* a redraw is ~100 ms: not for every 32 KB */
    }
    last_pct = pct;

    title("preparing the game data");
    screen_text(-1, 6, what, WHITE, BG);
    screen_bar(8, pct, GREEN, BG);
    char buf[24];
    snprintf(buf, sizeof buf, "%d %%", pct);
    screen_text(-1, 10, buf, GREY, BG);
    present_text();
}

static void data_error_screen(const char *dir)
{
    char l[64];
    switch (outrun_data_error())
    {
    case OUTRUN_DATA_ERR_NO_SD:
        say("No SD card", "Put the card in and restart:", "hold BOOT + PWR for 2 s, PLAY again.", NULL, NULL);
        break;
    case OUTRUN_DATA_ERR_NO_ROMS:
        snprintf(l, sizeof l, "%.34s", dir);
        say("No OutRun game data", "Copy outrun-data.bin or the 31", "MAME 'outrun' (set B) ROM files to:", l, NULL);
        break;
    case OUTRUN_DATA_ERR_MISSING:
    {
        snprintf(l, sizeof l, "%d of the 31 ROM files are missing:", outrun_data_error_detail());
        char f[40] = "";
        for (int i = 0; i < outrun_data_bad_file_count() && i < OUTRUN_DATA_MAX_BAD_FILES; i++)
        {
            strncat(f, outrun_data_bad_file(i), sizeof f - strlen(f) - 2);
            strncat(f, " ", sizeof f - strlen(f) - 1);
        }
        say("Incomplete ROM set", l, f, "needs MAME 'outrun' revision B", NULL);
        break;
    }
    case OUTRUN_DATA_ERR_BAD_CRC:
    {
        snprintf(l, sizeof l, "%d file(s) with the wrong contents:", outrun_data_error_detail());
        char f[40] = "";
        for (int i = 0; i < outrun_data_bad_file_count() && i < OUTRUN_DATA_MAX_BAD_FILES; i++)
        {
            strncat(f, outrun_data_bad_file(i), sizeof f - strlen(f) - 2);
            strncat(f, " ", sizeof f - strlen(f) - 1);
        }
        say("Wrong or damaged ROMs", l, f, "needs MAME 'outrun' revision B", NULL);
        break;
    }
    case OUTRUN_DATA_ERR_READ:
        snprintf(l, sizeof l, "error %d", outrun_data_error_detail());
        say("SD card read error", l, NULL, NULL, NULL);
        break;
    case OUTRUN_DATA_ERR_NO_MEMORY:
        snprintf(l, sizeof l, "%d KB of PSRAM short", outrun_data_error_detail());
        say("Not enough memory", l, NULL, NULL, NULL);
        break;
    default:
        say("The game data is unusable", "(a bug in the image builder)", NULL, NULL, NULL);
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* The game                                                                   */
/* ------------------------------------------------------------------------- */

static unsigned s_pictures; /* pictures presented since the last report */

static void report(void)
{
    static int64_t last_us;
    const int64_t now = esp_timer_get_time();
    if (last_us == 0)
    {
        last_us = now;
        return;
    }
    if (now - last_us < 5000000)
    {
        return;
    }
    const float secs = (float)(now - last_us) / 1e6f;
    last_us = now;

    unsigned slots, ticks, drawn;
    engine_stats(&slots, &ticks);
    drawn = s_pictures;
    s_pictures = 0;
    uint32_t played, dropped, starved;
    esp_audio_stats(&played, &dropped, &starved);
    ESP_LOGI(TAG, "slots %.1f/s ticks %.1f/s drawn %.1f/s | audio played %lu dropped %lu starved %lu | "
                  "free int %u psram %u",
             slots / secs, ticks / secs, drawn / secs, (unsigned long)played, (unsigned long)dropped,
             (unsigned long)starved, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

static void game_loop(void)
{
    const int64_t SLOT_US = 1000000 / 60;
    const int MAX_CATCH_UP = 4; /* slots of logic per picture, then let the game slow down rather than chase the clock */

    int64_t next = esp_timer_get_time();
    for (;;)
    {
        int64_t now = esp_timer_get_time();
        if (now < next)
        {
            if (next - now > 1500)
            {
                vTaskDelay(1);
            }
            continue;
        }

        bool render = false;
        int slots = 0;
        while (now >= next && slots < MAX_CATCH_UP)
        {
            render |= engine_slot();
            next += SLOT_US;
            slots++;
            now = esp_timer_get_time();
        }
        if (now >= next)
        {
            next = now; /* forgive the debt */
        }

        if (render)
        {
            screen_begin();
            engine_render();
            screen_present();
            s_pictures++;
        }
        report();
    }
}

static void game_task(void *)
{
    char dir[96];
    racer_game_path(dir, sizeof dir, "data", false); /* no folders can be made before the card is mounted */

    if (!board_sd_mount())
    {
        ESP_LOGE(TAG, "no SD card");
        outrun_data_init(dir, false, NULL);
        data_error_screen(dir);
        vTaskDelete(NULL);
        return;
    }

    racer_game_path(dir, sizeof dir, "data", true);
    title("preparing the game data");
    present_text();
    if (!outrun_data_init(dir, true, progress_screen))
    {
        data_error_screen(dir);
        vTaskDelete(NULL);
        return;
    }
    if (outrun_data_built_from_roms())
    {
        /* so the next start reads one file instead of decoding 31 */
        outrun_data_save_image(dir, progress_screen);
    }

    if (!engine_init())
    {
        say("The engine would not start", "see the serial log", NULL, NULL, NULL);
        vTaskDelete(NULL);
        return;
    }

    screen_blank_all();
    if (!esp_audio_start())
    {
        ESP_LOGW(TAG, "no audio");
    }
    esp_audio_start_task();
    ESP_LOGI(TAG, "running; free internal %u, psram %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    game_loop();
}

/* BOOT + PWR for 2 s: give the console its long-press power-off back before returning to the menu */
extern "C" void racer_before_exit(void)
{
    board_pwr_longpress_shutdown(true);
}

extern "C" void app_main(void)
{
    board_init();
    racer_game_start(); /* one-shot launch; BOOT + PWR held 2 s: back to the launcher */
    /* PWR is a held button here (brake): a 10 s hold must not switch the console off */
    board_pwr_longpress_shutdown(false);
    screen_init();
    board_backlight(80);
    esp_console_start();

    /* the engine goes deep: its own task with a big stack, on core 0 (core 1 has the codec) */
    xTaskCreatePinnedToCore(game_task, "outrun", 32 * 1024, NULL, 4, NULL, 0);
}
