/*
 * The globals cannonball/src/main/main.cpp would have defined, and the engine boot sequence
 * (port/glue.cpp on the Pico, hosttest/outrun_host.cpp on the PC).
 */

#include "esp_glue.hpp"

#include <stdio.h>

#include "esp_log.h"

#include "frontend/config.hpp"
#include "globals.hpp"
#include "main.hpp"
#include "roms.hpp"
#include "video.hpp"

#include "engine/audio/osoundint.hpp"
#include "engine/oinputs.hpp"
#include "engine/outrun.hpp"

#include "esp_audio.hpp"
#include "esp_input.hpp"
#include "outrun_data.h"
#include "sdl2/input.hpp"

static const char *TAG = "glue";

namespace cannonball
{
    Audio audio;
    int frame = 0;
    bool tick_frame = true;
    double frame_ms = 0;
    int fps_counter = 0;
    int state = STATE_BOOT;
}

static unsigned s_slots, s_ticks;

/* Point the engine's RomLoader instances at the data image. Every region is already decoded and resident,
 * so they only need a pointer and a length. tiles, sprites and road are absent on purpose: video.cpp takes
 * those straight from the image under OUTRUN_GFX_IN_FLASH. */
static bool bind_roms(void)
{
    struct
    {
        RomLoader *dst;
        outrun_region_t region;
        const char *name;
    } bind[] = {
        {&roms.rom0, OUTRUN_REGION_ROM0, "rom0"},
        {&roms.rom1, OUTRUN_REGION_ROM1, "rom1"},
        {&roms.z80, OUTRUN_REGION_Z80, "z80"},
        {&roms.pcm, OUTRUN_REGION_PCM, "pcm"},
    };

    for (auto &b : bind)
    {
        uint32_t size = 0;
        const uint8_t *p = outrun_data_region(b.region, &size);
        if (!p)
        {
            ESP_LOGE(TAG, "region %s missing from the data image", b.name);
            return false;
        }
        b.dst->set_flash(p, size);
    }
    return true;
}

bool engine_init(void)
{
    if (!outrun_data_valid() || !bind_roms())
    {
        return false;
    }

    config.set_fps(config.video.fps);

    /* two gear buttons (touch left = LOW, touch right = HIGH) instead of one that toggles */
    config.controls.gear = config.controls.GEAR_SEPARATE;

    esp_input_init();

    if (!video.init(&roms, &config.video))
    {
        ESP_LOGE(TAG, "video.init failed");
        return false;
    }

    // set_fps() deliberately does not do this - see port/config.cpp
    osoundint.init();
    esp_audio_reset();
    outrun.init();

    cannonball::state = cannonball::STATE_GAME;
    return true;
}

bool engine_slot(void)
{
    using namespace cannonball;

    frame++;
    s_slots++;

    /* Upstream paced its main loop at config.fps. Here a slot is 1/60 s, so at 30 run the engine on every
     * other slot. At 60 run every slot, but tick sprite logic on alternate ones (upstream's own
     * behaviour: the original scrolls the background at 60 but ticks sprites at 30). */
    bool run_engine;
    if (config.fps == 60)
    {
        run_engine = true;
        tick_frame = frame & 1;
    }
    else
    {
        run_engine = (frame & 1) == 0;
        tick_frame = true;
    }

    if (!run_engine)
    {
        return false;
    }

    if (tick_frame)
    {
        esp_input_poll();
        oinputs.tick();    // controls
        oinputs.do_gear(); // digital gear
    }

    outrun.tick(tick_frame);
    if (tick_frame)
    {
        input.frame_done();
    }

    /* the sound program and the sound chips run on core 1 (esp_audio_start_task); this side only queues commands */

    s_ticks++;
    return true;
}

void engine_render(void)
{
    video.prepare_frame();
    video.render_frame();
}

void engine_stats(unsigned *slots, unsigned *ticks)
{
    *slots = s_slots;
    *ticks = s_ticks;
    s_slots = s_ticks = 0;
}
